// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "debugutil_p.h"
#include "qqmldebugprocess_p.h"

#include <private/qqmldebugclient_p.h>
#include <private/qqmldebugconnection_p.h>
#include <private/qpacket_p.h>
#include <private/qv4debugclient_p.h>

#include <QtCore/qhash.h>
#include <QtCore/qlibraryinfo.h>
#include <QtCore/qmap.h>
#include <QtCore/qurl.h>
#include <QtTest/qtest.h>

struct CoverageFile
{
    QString url;
    QString source;
    QHash<QString, quint64> calls; // by function name
    QMap<int, quint64> lines;
};

struct CoverageReport
{
    quint32 round = 0;
    qint32 roundSize = 0;
    QList<CoverageFile> files;

    CoverageFile file(const QString &url) const
    {
        for (const CoverageFile &file : files) {
            if (file.url == url)
                return file;
        }
        return CoverageFile();
    }
};

class QQmlCoverageClient : public QQmlDebugClient
{
    Q_OBJECT
public:
    explicit QQmlCoverageClient(QQmlDebugConnection *connection)
        : QQmlDebugClient(QLatin1String("CodeCoverage"), connection)
    {
    }

    void requestSnapshot(quint32 round)
    {
        QPacket packet(connection()->currentDataStreamVersion());
        packet << QByteArray("SNAPSHOT") << round;
        sendMessage(packet.data());
    }

    void sendRaw(const QByteArray &message) { sendMessage(message); }

    // The first report of that round holding the file, or null. Each engine reports on its own,
    // and the qml tool runs a short-lived engine for its configuration before the program's.
    const CoverageReport *report(quint32 round, const QString &url) const
    {
        for (const CoverageReport &report : reports) {
            if (report.round == round && !report.file(url).url.isEmpty())
                return &report;
        }
        return nullptr;
    }

    // Rounds of the reports that answered a snapshot request, in order of arrival. A final report
    // is round 0 of a round of size 0.
    QList<quint32> snapshotRounds() const
    {
        QList<quint32> rounds;
        for (const CoverageReport &report : reports) {
            if (report.round != 0 || report.roundSize != 0)
                rounds.append(report.round);
        }
        return rounds;
    }

    QList<CoverageReport> reports;

protected:
    void messageReceived(const QByteArray &data) override
    {
        QPacket packet(connection()->currentDataStreamVersion(), data);
        QByteArray command;
        CoverageReport report;
        qint32 fileCount = 0;
        packet >> command >> report.round >> report.roundSize >> fileCount;
        QCOMPARE(command, QByteArray("REPORT"));
        QVERIFY(fileCount >= 0);
        for (qint32 i = 0; i < fileCount; ++i) {
            CoverageFile file;
            qint32 functionCount = 0;
            packet >> file.url >> file.source >> functionCount;
            QVERIFY(functionCount >= 0);
            for (qint32 j = 0; j < functionCount; ++j) {
                QString name;
                qint32 line = 0;
                quint64 calls = 0;
                packet >> name >> line >> calls;
                file.calls[name] += calls;
            }
            qint32 lineCount = 0;
            packet >> lineCount;
            QVERIFY(lineCount >= 0);
            for (qint32 j = 0; j < lineCount; ++j) {
                qint32 line = 0;
                quint64 hits = 0;
                packet >> line >> hits;
                QVERIFY(line > 0);
                file.lines.insert(line, hits);
            }
            report.files.append(file);
        }
        QVERIFY(packet.atEnd());
        reports.append(report);
    }
};

class tst_QQmlCoverageService : public QQmlDebugTest
{
    Q_OBJECT

public:
    tst_QQmlCoverageService();

private slots:
    void reportsWhenTheEngineGoesAway();
    void reportsUnexecutedCodeAsZero();
    void snapshotAnswersWithItsRound();
    void snapshotHoldsOnlyHitsSinceThePreviousReport();
    void ignoresMalformedRequests();
    void standsAsideForTheDebugger();
    void reportsCodeOnTheFirstLine();
    void sendsQrcSourceOncePerEngine();
    void answersWhenNoEngineIsLeft();
    void announcesVersionOne();
    void snapshotAsksEveryEngine();
    void reportsOnlyTheEngineThatGoesAway();
    void finalReportAnswersAPendingSnapshot();
    void finalReportAfterAnAnswerOwesNothing();
    void attachesNothingUntilEnabled();
    void staysQuietBesideTheDebuggerUntilEnabled();

private:
    // The clients the next connection opens; each enables the service of its name.
    enum class Clients { Coverage, CoverageAndDebugger, Debugger, Messages };

    ConnectResult launch(const QString &file);
    ConnectResult launchHelper(const QString &qml, const QStringList &environment = { });
    QString url(const QString &file) const;
    QList<QQmlDebugClient *> createClients() override;

    Clients m_clients = Clients::Coverage;
    QPointer<QQmlCoverageClient> m_client;
    QPointer<QV4DebugClient> m_debuggerClient;
};

tst_QQmlCoverageService::tst_QQmlCoverageService() : QQmlDebugTest(QT_QMLTEST_DATADIR) { }

QList<QQmlDebugClient *> tst_QQmlCoverageService::createClients()
{
    switch (m_clients) {
    case Clients::Coverage:
        m_client = new QQmlCoverageClient(m_connection);
        return QList<QQmlDebugClient *>({ m_client });
    case Clients::CoverageAndDebugger:
        m_client = new QQmlCoverageClient(m_connection);
        m_debuggerClient = new QV4DebugClient(m_connection);
        return QList<QQmlDebugClient *>({ m_client, m_debuggerClient });
    case Clients::Debugger:
        m_debuggerClient = new QV4DebugClient(m_connection);
        return QList<QQmlDebugClient *>({ m_debuggerClient });
    case Clients::Messages:
        return QList<QQmlDebugClient *>(
                { new QQmlDebugClient(QLatin1String("DebugMessages"), m_connection) });
    }
    Q_UNREACHABLE_RETURN({ });
}

QQmlDebugTest::ConnectResult tst_QQmlCoverageService::launch(const QString &file)
{
    m_clients = Clients::Coverage;
    // "block": the engine must not load code before the service is enabled.
    return connectTo(QLibraryInfo::path(QLibraryInfo::BinariesPath) + QLatin1String("/qml"),
                     QLatin1String("CodeCoverage"), testFile(file), true);
}

QQmlDebugTest::ConnectResult tst_QQmlCoverageService::launchHelper(const QString &qml,
                                                                   const QStringList &environment)
{
    m_clients = Clients::Coverage;
    return connectTo(QCoreApplication::applicationDirPath()
                             + QLatin1String("/qqmlcoverageservicehelper"),
                     QLatin1String("CodeCoverage"), qml, true, environment);
}

QString tst_QQmlCoverageService::url(const QString &file) const
{
    return QUrl::fromLocalFile(testFile(file)).toString();
}

// exit.qml: sumTo(3) once, which calls add() three times, then quits.
void tst_QQmlCoverageService::reportsWhenTheEngineGoesAway()
{
    QCOMPARE(launch(QLatin1String("exit.qml")), ConnectSuccess);
    QTRY_VERIFY(m_client->report(0, url(QLatin1String("exit.qml"))));

    QCOMPARE(m_client->snapshotRounds(), QList<quint32>());
    const CoverageFile file = m_client->report(0, url(QLatin1String("exit.qml")))
                                      ->file(url(QLatin1String("exit.qml")));
    QVERIFY(file.source.isEmpty()); // a local file: the client reads it itself
    QCOMPARE(file.calls.value(QLatin1String("sumTo")), 1u);
    QCOMPARE(file.calls.value(QLatin1String("add")), 3u);
    QMap<int, quint64> lines = file.lines;
    QEXPECT_FAIL("", "QTBUG-151779: a for header counts once per loop entry", Continue);
    QCOMPARE(lines.take(8), 4u); // the condition is tested four times
    // Line 11, the closing brace of sumTo(), follows its return and holds no code.
    // Line 16, the closing brace of the object, holds no code.
    const QMap<int, quint64> others({ { 4, 3 },
                                      { 5, 0 },
                                      { 6, 1 },
                                      { 7, 1 },
                                      { 9, 3 },
                                      { 10, 1 },
                                      { 12, 1 },
                                      { 13, 1 },
                                      { 14, 1 },
                                      { 15, 1 } });
    QCOMPARE(lines, others);
}

void tst_QQmlCoverageService::reportsUnexecutedCodeAsZero()
{
    QCOMPARE(launch(QLatin1String("exit.qml")), ConnectSuccess);
    QTRY_VERIFY(m_client->report(0, url(QLatin1String("exit.qml"))));

    const CoverageFile file = m_client->report(0, url(QLatin1String("exit.qml")))
                                      ->file(url(QLatin1String("exit.qml")));
    QVERIFY(file.calls.contains(QLatin1String("neverCalled")));
    QCOMPARE(file.calls.value(QLatin1String("neverCalled")), 0u);
    QCOMPARE(file.lines.value(5), 0u); // its body
}

// running.qml: once() on completion, tick() every 10 ms, never quits.
void tst_QQmlCoverageService::snapshotAnswersWithItsRound()
{
    QCOMPARE(launch(QLatin1String("running.qml")), ConnectSuccess);
    m_client->requestSnapshot(7);
    QTRY_VERIFY(m_client->report(7, url(QLatin1String("running.qml"))));

    const CoverageReport *report = m_client->report(7, url(QLatin1String("running.qml")));
    QCOMPARE(report->roundSize, 1);
    const CoverageFile file = report->file(url(QLatin1String("running.qml")));
    QCOMPARE(file.calls.value(QLatin1String("once")), 1u);
}

void tst_QQmlCoverageService::snapshotHoldsOnlyHitsSinceThePreviousReport()
{
    QCOMPARE(launch(QLatin1String("running.qml")), ConnectSuccess);
    m_client->requestSnapshot(1);
    QTRY_VERIFY(m_client->report(1, url(QLatin1String("running.qml"))));
    m_client->requestSnapshot(2);
    QTRY_VERIFY(m_client->report(2, url(QLatin1String("running.qml"))));

    const CoverageFile file = m_client->report(2, url(QLatin1String("running.qml")))
                                      ->file(url(QLatin1String("running.qml")));
    QVERIFY(file.lines.contains(5)); // still reported, ...
    QCOMPARE(file.calls.value(QLatin1String("once")), 0u); // ... but not counted again
}

void tst_QQmlCoverageService::ignoresMalformedRequests()
{
    QCOMPARE(launch(QLatin1String("running.qml")), ConnectSuccess);
    m_client->requestSnapshot(0); // round 0 is reserved for final reports
    QPacket unknown(m_connection->currentDataStreamVersion());
    unknown << QByteArray("BOGUS") << quint32(3);
    m_client->sendRaw(unknown.data());
    QPacket after(m_connection->currentDataStreamVersion()); // sorts after SNAPSHOT
    after << QByteArray("UNKNOWN") << quint32(5);
    m_client->sendRaw(after.data());
    QPacket trailing(m_connection->currentDataStreamVersion());
    trailing << QByteArray("SNAPSHOT") << quint32(6) << quint8(0);
    m_client->sendRaw(trailing.data());
    m_client->sendRaw(QByteArray("\x01\x02\x03", 3));
    m_client->requestSnapshot(4);

    // Requests are handled in order, so an answer to any of the bad ones would come first.
    QTRY_VERIFY(m_client->report(4, url(QLatin1String("running.qml"))));
    QCOMPARE(m_client->snapshotRounds(), QList<quint32>({ 4 }));
}

// An engine holds one debugger, and the QML debugger wins.
void tst_QQmlCoverageService::standsAsideForTheDebugger()
{
    m_clients = Clients::CoverageAndDebugger;
    QCOMPARE(connectTo(QLibraryInfo::path(QLibraryInfo::BinariesPath) + QLatin1String("/qml"),
                       QLatin1String("CodeCoverage,V8Debugger"),
                       testFile(QLatin1String("exit.qml")), true),
             ConnectSuccess);
    m_debuggerClient->connect(); // the blocked program runs once the debugger connected
    QVERIFY(m_process->waitForFinished());
    QCOMPARE(m_process->exitStatus(), QProcess::NormalExit);
    QVERIFY(m_process->output().contains(QLatin1String(
            "CodeCoverage: a QML debugger service is loaded too; coverage disabled")));
    QCOMPARE(m_client->reports.size(), 0);
}

// firstline.qml calls first() from first.js, whose code starts on line 1, the line its root
// function (the file's own code) starts on too.
void tst_QQmlCoverageService::reportsCodeOnTheFirstLine()
{
    QCOMPARE(launch(QLatin1String("firstline.qml")), ConnectSuccess);
    QTRY_VERIFY(m_client->report(0, url(QLatin1String("first.js"))));

    const CoverageFile file = m_client->report(0, url(QLatin1String("first.js")))
                                      ->file(url(QLatin1String("first.js")));
    QCOMPARE(file.calls.value(QLatin1String("first")), 1u);
    QCOMPARE(file.lines.value(1), 2u); // the file's code once, first() once: summed
}

// The client cannot read qrc: files, so their source comes along, once per engine.
void tst_QQmlCoverageService::sendsQrcSourceOncePerEngine()
{
    const QString resource = QLatin1String("qrc:/resource.qml");
    QCOMPARE(launchHelper(resource), ConnectSuccess);
    m_client->requestSnapshot(1);
    QTRY_VERIFY(m_client->report(1, resource));
    m_client->requestSnapshot(2);
    QTRY_VERIFY(m_client->report(2, resource));

    QVERIFY(m_client->report(1, resource)
                    ->file(resource)
                    .source.contains(QLatin1String("function once() { return 1 }")));
    QCOMPARE(m_client->report(2, resource)->file(resource).source, QString());
}

void tst_QQmlCoverageService::answersWhenNoEngineIsLeft()
{
    QCOMPARE(launchHelper(testFile(QLatin1String("running.qml")),
                          { QLatin1String("COVERAGE_HELPER_RELEASE=1") }),
             ConnectSuccess);
    QTRY_VERIFY(m_client->report(0, url(QLatin1String("running.qml"))));
    m_client->requestSnapshot(3);
    QTRY_COMPARE(m_client->snapshotRounds(), QList<quint32>({ 3 }));

    const CoverageReport answer = m_client->reports.last();
    QCOMPARE(answer.roundSize, 0);
    QCOMPARE(answer.files.size(), 0);
}

void tst_QQmlCoverageService::announcesVersionOne()
{
    QCOMPARE(launch(QLatin1String("exit.qml")), ConnectSuccess);
    QCOMPARE(m_client->serviceVersion(), 1.0f);
}

void tst_QQmlCoverageService::snapshotAsksEveryEngine()
{
    const QString resource = QLatin1String("qrc:/resource.qml");
    QCOMPARE(launchHelper(testFile(QLatin1String("running.qml")) + QLatin1Char(',') + resource),
             ConnectSuccess);
    m_client->requestSnapshot(1);
    QTRY_COMPARE(m_client->snapshotRounds(), QList<quint32>({ 1, 1 }));

    QCOMPARE(m_client->report(1, url(QLatin1String("running.qml")))->roundSize, 2);
    QCOMPARE(m_client->report(1, resource)->roundSize, 2);
}

// The engine of resource.qml goes away; the one of running.qml stays.
void tst_QQmlCoverageService::reportsOnlyTheEngineThatGoesAway()
{
    const QString resource = QLatin1String("qrc:/resource.qml");
    QCOMPARE(launchHelper(testFile(QLatin1String("running.qml")) + QLatin1Char(',') + resource,
                          { QLatin1String("COVERAGE_HELPER_RELEASE=1") }),
             ConnectSuccess);
    QTRY_VERIFY(m_client->report(0, resource));
    m_client->requestSnapshot(1);
    QTRY_COMPARE(m_client->snapshotRounds(), QList<quint32>({ 1 }));

    QVERIFY(!m_client->report(0, url(QLatin1String("running.qml"))));
    QCOMPARE(m_client->report(1, url(QLatin1String("running.qml")))->roundSize, 1);
}

// The engine of resource.qml goes away while a snapshot request waits for it to run.
void tst_QQmlCoverageService::finalReportAnswersAPendingSnapshot()
{
    const QString resource = QLatin1String("qrc:/resource.qml");
    QCOMPARE(launchHelper(testFile(QLatin1String("running.qml")) + QLatin1Char(',') + resource,
                          { QLatin1String("COVERAGE_HELPER_RELEASE=1"),
                            QLatin1String("COVERAGE_HELPER_STALL=1000") }),
             ConnectSuccess);
    QTRY_VERIFY(m_process->output().contains(QLatin1String("stalling")));
    m_client->requestSnapshot(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_client->snapshotRounds(), QList<quint32>({ 1, 1 }), 10000);

    QCOMPARE(m_client->report(1, resource)->roundSize, 2);
    QCOMPARE(m_client->report(1, resource)->file(resource).calls.value(QLatin1String("once")), 1u);
    QCOMPARE(m_client->report(1, url(QLatin1String("running.qml")))->roundSize, 2);
}

// The engine of resource.qml answers a snapshot request, then goes away.
void tst_QQmlCoverageService::finalReportAfterAnAnswerOwesNothing()
{
    const QString resource = QLatin1String("qrc:/resource.qml");
    QCOMPARE(launchHelper(testFile(QLatin1String("running.qml")) + QLatin1Char(',') + resource,
                          { QLatin1String("COVERAGE_HELPER_RELEASE_AFTER=2000") }),
             ConnectSuccess);
    m_client->requestSnapshot(1);
    QTRY_COMPARE(m_client->snapshotRounds(), QList<quint32>({ 1, 1 }));
    QTRY_VERIFY_WITH_TIMEOUT(m_client->report(0, resource), 10000);

    QCOMPARE(m_client->snapshotRounds(), QList<quint32>({ 1, 1 }));
}

// Loaded, but no client asked for coverage: the engine must keep its speed.
void tst_QQmlCoverageService::attachesNothingUntilEnabled()
{
    m_clients = Clients::Messages;
    QCOMPARE(connectTo(QCoreApplication::applicationDirPath()
                               + QLatin1String("/qqmlcoverageservicehelper"),
                       QLatin1String("CodeCoverage,DebugMessages"),
                       testFile(QLatin1String("running.qml")), true),
             ConnectSuccess);
    QTRY_VERIFY(m_process->output().contains(QLatin1String("running.qml: debugger ")));
    QVERIFY(m_process->output().contains(QLatin1String("running.qml: debugger none")));
}

void tst_QQmlCoverageService::staysQuietBesideTheDebuggerUntilEnabled()
{
    m_clients = Clients::Debugger;
    QCOMPARE(connectTo(QLibraryInfo::path(QLibraryInfo::BinariesPath) + QLatin1String("/qml"),
                       QLatin1String("CodeCoverage,V8Debugger"),
                       testFile(QLatin1String("exit.qml")), true),
             ConnectSuccess);
    m_debuggerClient->connect();
    QVERIFY(m_process->waitForFinished());
    QCOMPARE(m_process->exitStatus(), QProcess::NormalExit);
    QVERIFY(!m_process->output().contains(QLatin1String("CodeCoverage")));
}

QTEST_MAIN(tst_QQmlCoverageService)

#include "tst_qqmlcoverageservice.moc"
