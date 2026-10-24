// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "qmlcoveragereport.h"

#include <private/qpacket_p.h>

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qurl.h>
#include <QtTest/qtest.h>

using namespace Qt::StringLiterals;

namespace {

constexpr int streamVersion = QDataStream::Qt_DefaultCompiledVersion;

struct FunctionRecord
{
    QString name;
    qint32 line = 0;
    quint64 calls = 0;
};

struct FileRecord
{
    QString url;
    QString source;
    QList<FunctionRecord> functions;
    QList<std::pair<qint32, quint64>> lines;
};

// A message as the CodeCoverage service sends it.
QByteArray reportMessage(quint32 round, qint32 roundSize, const QList<FileRecord> &files)
{
    QPacket packet(streamVersion);
    packet << QByteArray("REPORT") << round << roundSize << qint32(files.size());
    for (const FileRecord &file : files) {
        packet << file.url << file.source << qint32(file.functions.size());
        for (const FunctionRecord &function : file.functions)
            packet << function.name << function.line << function.calls;
        packet << qint32(file.lines.size());
        for (const auto &[line, hits] : file.lines)
            packet << line << hits;
    }
    return packet.data();
}

// The lcov records of one source file, without the TN: and SF: lines.
QStringList lcovRecords(const QString &lcov, const QString &path)
{
    const QStringList lines = lcov.split(u'\n');
    const qsizetype start = lines.indexOf(u"SF:"_s + path);
    if (start < 0)
        return { };
    const qsizetype end = lines.indexOf(u"end_of_record"_s, start);
    return lines.mid(start + 1, end - start - 1);
}

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll())
                                                            : QString();
}

} // namespace

class tst_QmlCoverageReport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void decodesAReportWithNoFiles();
    void decodesOneFile();
    void sumsReportsThatShareAFile();
    void sumsAFunctionListedTwice();
    void keepsTheSourceOnceSent();
    void refusesAnotherCommand();
    void refusesTrailingBytes();
    void refusesANegativeCount();
    void refusesAReportCutBeforeItsFileCount();
    void refusesAFileCountTheBytesCannotHold();
    void refusesAFunctionCountTheBytesCannotHold();
    void refusesALineCountTheBytesCannotHold();
    void writesEveryLineTheEngineReports();
    void namesAnAnonymousFunctionByItsLine();
    void namesFunctionsThatShareANameByTheirLines();
    void countsOnlyCalledFunctionsAsHit();
    void writesTheSourceOfAQrcFile();
    void skipsAFileWithoutSource();
    void writesTheFilesAfterASkippedOne();
    void failsOnAnUnwritableReport();
    void failsOnAnUnwritableSource();
    void failsOnAFullDisk();
    void collectorSumsReports();
    void collectorCountsMalformedMessages();
    void reportBeforeASnapshotCompletesNothing();
    void snapshotOfNoEngineCompletesOnItsAnswer();
    void snapshotOfOneEngineCompletesOnItsAnswer();
    void snapshotOfTwoEnginesWaitsForBoth();
    void answerToAnEarlierSnapshotCompletesNothing();
    void finalReportCompletesNothing();
    void snapshotCompletesOnce();
    void nextSnapshotCountsItsOwnAnswers();

private:
    QString lcov() const { return readAll(m_output); }

    QTemporaryDir m_dir;
    QString m_output;
};

void tst_QmlCoverageReport::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void tst_QmlCoverageReport::init()
{
    m_output = m_dir.filePath(QLatin1StringView(QTest::currentTestFunction()) + u".info"_s);
}

void tst_QmlCoverageReport::decodesAReportWithNoFiles()
{
    const auto report = QmlCoverage::decodeReport(reportMessage(3, 2, { }), streamVersion);
    QVERIFY(report);
    QCOMPARE(report->round, 3u);
    QCOMPARE(report->roundSize, 2);
    QCOMPARE(report->files.size(), 0);
}

void tst_QmlCoverageReport::decodesOneFile()
{
    const FileRecord file{
        u"file:///a.qml"_s, QString(), { { u"f"_s, 2, 5 } }, { { 2, 7 }, { 3, 0 } }
    };
    const auto report = QmlCoverage::decodeReport(reportMessage(0, 0, { file }), streamVersion);
    QVERIFY(report);
    const QmlCoverage::File decoded = report->files.value(file.url);
    QCOMPARE(decoded.functions.value({ 2, u"f"_s }), 5u);
    QCOMPARE(decoded.lines, (QMap<int, quint64>{ { 2, 7 }, { 3, 0 } }));
}

void tst_QmlCoverageReport::sumsReportsThatShareAFile()
{
    const FileRecord first{
        u"file:///a.qml"_s, QString(), { { u"f"_s, 2, 1 } }, { { 2, 1 }, { 3, 0 } }
    };
    const FileRecord second{
        u"file:///a.qml"_s, QString(), { { u"f"_s, 2, 2 } }, { { 2, 2 }, { 3, 1 } }
    };
    const FileRecord other{ u"file:///b.qml"_s, QString(), { }, { { 1, 1 } } };
    QmlCoverage::Files totals;
    QmlCoverage::merge(
            &totals,
            QmlCoverage::decodeReport(reportMessage(1, 1, { first }), streamVersion)->files);
    QmlCoverage::merge(
            &totals,
            QmlCoverage::decodeReport(reportMessage(2, 1, { second, other }), streamVersion)
                    ->files);
    QCOMPARE(totals.size(), 2);
    QCOMPARE(totals.value(first.url).functions.value({ 2, u"f"_s }), 3u);
    QCOMPARE(totals.value(first.url).lines, (QMap<int, quint64>{ { 2, 3 }, { 3, 1 } }));
}

// Two functions on one line with one name, such as two arrow functions, come as two entries.
void tst_QmlCoverageReport::sumsAFunctionListedTwice()
{
    const FileRecord file{
        u"file:///a.qml"_s, QString(), { { QString(), 3, 1 }, { QString(), 3, 2 } }, { }
    };
    const auto report = QmlCoverage::decodeReport(reportMessage(0, 0, { file }), streamVersion);
    QVERIFY(report);
    QCOMPARE(report->files.value(file.url).functions.value({ 3, QString() }), 3u);
}

// An engine sends a qrc: source in its first report only.
void tst_QmlCoverageReport::keepsTheSourceOnceSent()
{
    const QString url = u"qrc:/a.qml"_s;
    QmlCoverage::Files totals;
    QmlCoverage::merge(&totals, { { url, { QString(), { }, { } } } });
    QmlCoverage::merge(&totals, { { url, { u"source"_s, { }, { } } } });
    QmlCoverage::merge(&totals, { { url, { QString(), { }, { } } } });
    QCOMPARE(totals.value(url).source, u"source"_s);
}

void tst_QmlCoverageReport::refusesAnotherCommand()
{
    QByteArray message = reportMessage(1, 1, { });
    message.replace("REPORT", "REPORX");
    QVERIFY(!QmlCoverage::decodeReport(message, streamVersion));
}

void tst_QmlCoverageReport::refusesTrailingBytes()
{
    QVERIFY(!QmlCoverage::decodeReport(reportMessage(1, 1, { }) + '\0', streamVersion));
}

void tst_QmlCoverageReport::refusesANegativeCount()
{
    QPacket packet(streamVersion);
    packet << QByteArray("REPORT") << quint32(1) << qint32(1) << qint32(-1);
    QVERIFY(!QmlCoverage::decodeReport(packet.data(), streamVersion));
}

void tst_QmlCoverageReport::refusesAReportCutBeforeItsFileCount()
{
    QPacket packet(streamVersion);
    packet << QByteArray("REPORT") << quint32(1) << qint32(1);
    QVERIFY(!QmlCoverage::decodeReport(packet.data(), streamVersion));
}

void tst_QmlCoverageReport::refusesAFileCountTheBytesCannotHold()
{
    QPacket packet(streamVersion);
    packet << QByteArray("REPORT") << quint32(1) << qint32(1) << qint32(0x7ffffffe);
    QVERIFY(!QmlCoverage::decodeReport(packet.data(), streamVersion));
}

void tst_QmlCoverageReport::refusesAFunctionCountTheBytesCannotHold()
{
    QPacket packet(streamVersion);
    packet << QByteArray("REPORT") << quint32(1) << qint32(1) << qint32(1) << QString() << QString()
           << qint32(0x7ffffffe);
    QVERIFY(!QmlCoverage::decodeReport(packet.data(), streamVersion));
}

void tst_QmlCoverageReport::refusesALineCountTheBytesCannotHold()
{
    QPacket packet(streamVersion);
    packet << QByteArray("REPORT") << quint32(1) << qint32(1) << qint32(1) << QString() << QString()
           << qint32(0) << qint32(0x7ffffffe);
    QVERIFY(!QmlCoverage::decodeReport(packet.data(), streamVersion));
}

void tst_QmlCoverageReport::writesEveryLineTheEngineReports()
{
    const QString source = m_dir.filePath(u"statements.js"_s);
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("function f() {\n    return 1\n}\n");
    file.close();
    QmlCoverage::Files files;
    files.insert(QUrl::fromLocalFile(source).toString(),
                 { QString(), { { 1, 1 }, { 2, 1 }, { 3, 0 } }, { { { 1, u"f"_s }, 1 } } });
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, m_dir.filePath(u"src"_s), &skipped, &failed));
    QCOMPARE(lcovRecords(lcov(), source),
             QStringList({ u"FN:1,f"_s, u"FNDA:1,f"_s, u"FNF:1"_s, u"FNH:1"_s, u"DA:1,1"_s,
                           u"DA:2,1"_s, u"DA:3,0"_s, u"LF:3"_s, u"LH:2"_s }));
}

void tst_QmlCoverageReport::namesAnAnonymousFunctionByItsLine()
{
    QmlCoverage::Files files;
    files.insert(u"qrc:/a.qml"_s, { u"a\n"_s, { }, { { { 3, QString() }, 1 } } });
    const QString sources = m_dir.filePath(u"anonymous"_s);
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, sources, &skipped, &failed));
    QCOMPARE(lcovRecords(lcov(), sources + u"/qrc/a.qml"_s).filter(u"FN:"_s),
             QStringList({ u"FN:3,<anonymous>:3"_s }));
}

void tst_QmlCoverageReport::namesFunctionsThatShareANameByTheirLines()
{
    QmlCoverage::Files files;
    files.insert(u"qrc:/a.qml"_s,
                 { u"a\n"_s,
                   { },
                   { { { 3, u"expression for target"_s }, 1 },
                     { { 5, u"expression for target"_s }, 0 },
                     { { 7, u"onClicked"_s }, 1 } } });
    const QString sources = m_dir.filePath(u"shared"_s);
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, sources, &skipped, &failed));
    QCOMPARE(lcovRecords(lcov(), sources + u"/qrc/a.qml"_s).filter(u"FN:"_s),
             QStringList({ u"FN:3,expression for target:3"_s, u"FN:5,expression for target:5"_s,
                           u"FN:7,onClicked"_s }));
}

void tst_QmlCoverageReport::countsOnlyCalledFunctionsAsHit()
{
    QmlCoverage::Files files;
    files.insert(u"qrc:/a.qml"_s,
                 { u"a\n"_s, { }, { { { 3, u"called"_s }, 2 }, { { 5, u"idle"_s }, 0 } } });
    const QString sources = m_dir.filePath(u"hit"_s);
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, sources, &skipped, &failed));
    QCOMPARE(lcovRecords(lcov(), sources + u"/qrc/a.qml"_s).filter(u"FN"_s),
             QStringList({ u"FN:3,called"_s, u"FN:5,idle"_s, u"FNDA:2,called"_s, u"FNDA:0,idle"_s,
                           u"FNF:2"_s, u"FNH:1"_s }));
}

void tst_QmlCoverageReport::writesTheSourceOfAQrcFile()
{
    QmlCoverage::Files files;
    files.insert(u"qrc:/dir/a.qml"_s, { u"import QtQml\nQtObject {}\n"_s, { { 2, 1 } }, { } });
    const QString sources = m_dir.filePath(u"qrcsources"_s);
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, sources, &skipped, &failed));
    const QString copy = sources + u"/qrc/dir/a.qml"_s;
    QCOMPARE(readAll(copy), u"import QtQml\nQtObject {}\n"_s);
    QCOMPARE(lcovRecords(lcov(), copy).filter(u"DA:"_s), QStringList({ u"DA:2,1"_s }));
}

void tst_QmlCoverageReport::skipsAFileWithoutSource()
{
    QmlCoverage::Files files;
    files.insert(u"app:/a.qml"_s, { QString(), { { 1, 1 } }, { } });
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, m_dir.filePath(u"src"_s), &skipped, &failed));
    QCOMPARE(skipped, QStringList({ u"app:/a.qml"_s }));
    QCOMPARE(lcov(), QString());
}

void tst_QmlCoverageReport::writesTheFilesAfterASkippedOne()
{
    QmlCoverage::Files files;
    files.insert(u"app:/a.qml"_s, { QString(), { { 1, 1 } }, { } });
    files.insert(u"qrc:/b.qml"_s, { u"b\n"_s, { { 1, 1 } }, { } });
    const QString sources = m_dir.filePath(u"afterskipped"_s);
    QStringList skipped;
    QString failed;
    QVERIFY(QmlCoverage::writeLcov(files, m_output, sources, &skipped, &failed));
    QCOMPARE(lcovRecords(lcov(), sources + u"/qrc/b.qml"_s).filter(u"DA:"_s),
             QStringList({ u"DA:1,1"_s }));
}

void tst_QmlCoverageReport::failsOnAnUnwritableReport()
{
    const QString output = m_dir.filePath(u"missing/dir/report.info"_s);
    QStringList skipped;
    QString failed;
    QVERIFY(!QmlCoverage::writeLcov({ }, output, m_dir.filePath(u"src"_s), &skipped, &failed));
    QCOMPARE(failed, output);
}

// The source directory is a file, so the source cannot be written below it.
void tst_QmlCoverageReport::failsOnAnUnwritableSource()
{
    const QString sources = m_dir.filePath(u"notadir"_s);
    QFile blocker(sources);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QmlCoverage::Files files;
    files.insert(u"qrc:/a.qml"_s, { u"a\n"_s, { { 1, 1 } }, { } });
    QStringList skipped;
    QString failed;
    QVERIFY(!QmlCoverage::writeLcov(files, m_output, sources, &skipped, &failed));
    QCOMPARE(failed, sources + u"/qrc/a.qml"_s);
}

// Writes to /dev/full fail with ENOSPC.
void tst_QmlCoverageReport::failsOnAFullDisk()
{
#ifndef Q_OS_LINUX
    QSKIP("/dev/full is a Linux device");
#else
    QmlCoverage::Files files;
    files.insert(u"qrc:/a.qml"_s, { u"a\n"_s, { { 1, 1 } }, { } });
    QStringList skipped;
    QString failed;
    QVERIFY(!QmlCoverage::writeLcov(files, u"/dev/full"_s, m_dir.filePath(u"full"_s), &skipped,
                                    &failed));
    QCOMPARE(failed, u"/dev/full"_s);
#endif
}

void tst_QmlCoverageReport::collectorSumsReports()
{
    const FileRecord first{ u"file:///a.qml"_s, QString(), { }, { { 2, 1 } } };
    const FileRecord second{ u"file:///a.qml"_s, QString(), { }, { { 2, 2 } } };
    QmlCoverage::Collector collector;
    collector.receive(reportMessage(0, 0, { first }), streamVersion);
    collector.receive(reportMessage(0, 0, { second }), streamVersion);
    QCOMPARE(collector.reports(), 2);
    QCOMPARE(collector.malformed(), 0);
    QCOMPARE(collector.files().value(first.url).lines, (QMap<int, quint64>{ { 2, 3 } }));
}

void tst_QmlCoverageReport::collectorCountsMalformedMessages()
{
    QmlCoverage::Collector collector;
    collector.startSnapshot();
    QVERIFY(!collector.receive(QByteArray("\x01\x02\x03", 3), streamVersion));
    QCOMPARE(collector.reports(), 0);
    QCOMPARE(collector.malformed(), 1);
}

void tst_QmlCoverageReport::reportBeforeASnapshotCompletesNothing()
{
    QmlCoverage::Collector collector;
    QVERIFY(!collector.receive(reportMessage(0, 0, { }), streamVersion));
}

void tst_QmlCoverageReport::snapshotOfNoEngineCompletesOnItsAnswer()
{
    QmlCoverage::Collector collector;
    const quint32 round = collector.startSnapshot();
    QCOMPARE(round, 1u);
    QVERIFY(collector.receive(reportMessage(round, 0, { }), streamVersion));
}

void tst_QmlCoverageReport::snapshotOfOneEngineCompletesOnItsAnswer()
{
    QmlCoverage::Collector collector;
    const quint32 round = collector.startSnapshot();
    QVERIFY(collector.receive(reportMessage(round, 1, { }), streamVersion));
}

void tst_QmlCoverageReport::snapshotOfTwoEnginesWaitsForBoth()
{
    QmlCoverage::Collector collector;
    const quint32 round = collector.startSnapshot();
    QVERIFY(!collector.receive(reportMessage(round, 2, { }), streamVersion));
    QVERIFY(collector.receive(reportMessage(round, 2, { }), streamVersion));
}

void tst_QmlCoverageReport::answerToAnEarlierSnapshotCompletesNothing()
{
    QmlCoverage::Collector collector;
    const quint32 earlier = collector.startSnapshot();
    const quint32 round = collector.startSnapshot();
    QCOMPARE(round, earlier + 1);
    QVERIFY(!collector.receive(reportMessage(earlier, 1, { }), streamVersion));
    QVERIFY(collector.receive(reportMessage(round, 1, { }), streamVersion));
}

// An engine removed before the request reached it owed no answer.
void tst_QmlCoverageReport::finalReportCompletesNothing()
{
    QmlCoverage::Collector collector;
    const quint32 round = collector.startSnapshot();
    QVERIFY(!collector.receive(reportMessage(0, 0, { }), streamVersion));
    QVERIFY(collector.receive(reportMessage(round, 1, { }), streamVersion));
}

void tst_QmlCoverageReport::snapshotCompletesOnce()
{
    QmlCoverage::Collector collector;
    const quint32 round = collector.startSnapshot();
    QVERIFY(collector.receive(reportMessage(round, 1, { }), streamVersion));
    QVERIFY(!collector.receive(reportMessage(round, 1, { }), streamVersion));
}

void tst_QmlCoverageReport::nextSnapshotCountsItsOwnAnswers()
{
    QmlCoverage::Collector collector;
    const quint32 first = collector.startSnapshot();
    QVERIFY(collector.receive(reportMessage(first, 1, { }), streamVersion));
    const quint32 second = collector.startSnapshot();
    QVERIFY(!collector.receive(reportMessage(second, 2, { }), streamVersion));
}

QTEST_GUILESS_MAIN(tst_QmlCoverageReport)

#include "tst_qmlcoveragereport.moc"
