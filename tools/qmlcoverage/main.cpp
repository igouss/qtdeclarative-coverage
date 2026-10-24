// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// qmlcoverage: runs a QML program with the CodeCoverage debug service and writes lcov.
//
//   qmlcoverage -o coverage.info <program> [args...]
//
// On Unix, for a program that runs until stopped, send qmlcoverage SIGINT or SIGTERM (Ctrl+C): it
// takes a final snapshot from every engine, stops the program and writes the report.

#include "qmlcoveragereport.h"

#include <private/qpacket_p.h>
#include <private/qqmldebugclient_p.h>
#include <private/qqmldebugconnection_p.h>

#include <QtCore/qcommandlineparser.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qprocess.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qtimer.h>

#include <cstdio>
#include <functional>

#ifdef Q_OS_UNIX
#  include <QtCore/qsocketnotifier.h>

#  include <csignal>
#  include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#  include <sys/prctl.h>
#endif

using namespace Qt::StringLiterals;

namespace {

class CoverageClient : public QQmlDebugClient
{
public:
    explicit CoverageClient(QQmlDebugConnection *connection)
        : QQmlDebugClient(u"CodeCoverage"_s, connection)
    {
    }

    // Asks every engine for its hits so far; onSnapshotComplete runs once all of them answered.
    void requestSnapshot()
    {
        QPacket packet(connection()->currentDataStreamVersion());
        packet << QByteArray("SNAPSHOT") << collector.startSnapshot();
        sendMessage(packet.data());
    }

    QmlCoverage::Collector collector;
    std::function<void()> onSnapshotComplete;

private:
    void messageReceived(const QByteArray &message) override
    {
        if (collector.receive(message, connection()->currentDataStreamVersion()))
            onSnapshotComplete();
    }
};

#ifdef Q_OS_UNIX
int stopPipe[2] = { -1, -1 };

void requestStop(int)
{
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = ::write(stopPipe[1], &byte, 1);
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"qmlcoverage"_s);
    QCoreApplication::setApplicationVersion(QLatin1StringView(QT_VERSION_STR));

    QCommandLineParser parser;
    parser.setApplicationDescription(
            u"Runs a QML program and writes the QML and JavaScript lines and functions it "
            "executed as lcov.\nOn Unix, send SIGINT or SIGTERM to stop a long-running program "
            "and write what it covered."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
    const QCommandLineOption outputOption({ u"o"_s, u"output"_s }, u"lcov file to write."_s,
                                          u"file"_s, u"coverage.info"_s);
    const QCommandLineOption sourcesOption(
            u"sources"_s, u"Directory for sources the program sent (qrc:). Default: <output>.src"_s,
            u"dir"_s);
    parser.addOption(outputOption);
    parser.addOption(sourcesOption);
    parser.addPositionalArgument(u"program"_s, u"Program to run."_s);
    parser.addPositionalArgument(u"args"_s, u"Its arguments."_s, u"[args...]"_s);
    parser.process(app);

    QStringList command = parser.positionalArguments();
    if (command.isEmpty())
        parser.showHelp(2);
    const QString program = command.takeFirst();
    const QString output = parser.value(outputOption);
    const QString sourceDir =
            parser.isSet(sourcesOption) ? parser.value(sourcesOption) : output + u".src"_s;

    QTemporaryDir socketDir;
    if (!socketDir.isValid()) {
        std::fprintf(stderr, "qmlcoverage: cannot create a directory for the socket\n");
        return 2;
    }
    const QString socket = socketDir.filePath(u"coverage.sock"_s);

    QQmlDebugConnection connection;
    CoverageClient client(&connection); // must exist before the handshake lists clients
    connection.startLocalServer(socket);

    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // With stderr on a pipe, Qt may log to the journal instead; we forward stderr, so keep it.
    if (!env.contains(u"QT_FORCE_STDERR_LOGGING"_s))
        env.insert(u"QT_FORCE_STDERR_LOGGING"_s, u"1"_s);
    process.setProcessEnvironment(env);
    command.prepend(u"-qmljsdebugger=file:%1,block,services:CodeCoverage"_s.arg(socket));

    bool stopping = false;
#ifdef Q_OS_UNIX
    // Own process group, so a terminal's Ctrl+C reaches only us and we can snapshot first. Out of
    // the group, the program would outlive a qmlcoverage that is killed, on Linux at least.
    process.setChildProcessModifier([] {
        ::setpgid(0, 0);
#  ifdef Q_OS_LINUX
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
#  endif
    });

    bool snapshotTimedOut = false;
    QTimer snapshotTimeout;
    snapshotTimeout.setSingleShot(true);
    snapshotTimeout.setInterval(10000);
    const auto stopProgram = [&] {
        snapshotTimeout.stop();
        process.terminate();
    };
    client.onSnapshotComplete = stopProgram;
    // Said now: without any report, the exit code says only that nothing was covered.
    QObject::connect(&snapshotTimeout, &QTimer::timeout, &app, [&] {
        std::fprintf(stderr, "qmlcoverage: not every engine answered the final snapshot\n");
        snapshotTimedOut = true;
        stopProgram();
    });

    if (::pipe(stopPipe) != 0) {
        std::fprintf(stderr, "qmlcoverage: cannot create the signal pipe\n");
        return 2;
    }
    QSocketNotifier stopNotifier(stopPipe[0], QSocketNotifier::Read);
    QObject::connect(&stopNotifier, &QSocketNotifier::activated, &app, [&] {
        char byte;
        [[maybe_unused]] const ssize_t got = ::read(stopPipe[0], &byte, 1);
        if (stopping)
            return;
        stopping = true;
        if (client.state() != QQmlDebugClient::Enabled) {
            process.terminate();
            return;
        }
        client.requestSnapshot();
        snapshotTimeout.start();
    });
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
#endif

    int programExit = 0;
    bool crashed = false;
    QObject::connect(
            &process, &QProcess::finished, &app, [&](int code, QProcess::ExitStatus status) {
                programExit = code;
                crashed = status != QProcess::NormalExit;
                // Reports already sent are queued on the socket; drain them first.
                QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
            });
    process.start(program, command);
    if (!process.waitForStarted()) {
        std::fprintf(stderr, "qmlcoverage: cannot run %s: %s\n", qPrintable(program),
                     qPrintable(process.errorString()));
        return 2;
    }
    app.exec();

    if (crashed && !stopping) {
        std::fprintf(stderr, "qmlcoverage: %s crashed; no coverage written\n", qPrintable(program));
        return 3;
    }
    // No report means the service never ran: a silent empty lcov would read as "nothing covered".
    const QmlCoverage::Collector &collector = client.collector;
    if (collector.reports() == 0 || collector.malformed() > 0) {
        std::fprintf(stderr,
                     "qmlcoverage: %d coverage report(s), %d malformed; does the program enable "
                     "QML debugging, and is the qmldbg_coverage plugin of this Qt deployed?\n",
                     collector.reports(), collector.malformed());
        return 4;
    }

    QStringList skipped;
    QString failedPath;
    if (!QmlCoverage::writeLcov(collector.files(), output, sourceDir, &skipped, &failedPath)) {
        std::fprintf(stderr, "qmlcoverage: cannot write %s\n", qPrintable(failedPath));
        return 2;
    }
    for (const QString &url : std::as_const(skipped))
        std::fprintf(stderr, "qmlcoverage: no source for %s; not written\n", qPrintable(url));
    std::fprintf(stderr, "qmlcoverage: %d report(s), %lld file(s) -> %s\n", collector.reports(),
                 qlonglong(collector.files().size() - skipped.size()), qPrintable(output));
#ifdef Q_OS_UNIX
    if (snapshotTimedOut)
        return 5;
#endif
    return stopping ? 0 : programExit;
}
