// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qlibraryinfo.h>
#include <QtCore/qprocess.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qurl.h>
#include <QtTest/qtest.h>

#ifdef Q_OS_UNIX
#  include <csignal>
#  include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace {

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

// The records without their counts: which lines and functions a file has, not how often they ran.
QStringList withoutCounts(const QStringList &records)
{
    static const QRegularExpression count(QStringLiteral("^(DA:\\d+),\\d+$|^FNDA:\\d+,(.*)$"));
    static const QRegularExpression hit(QStringLiteral("^(LH|FNH):"));
    QStringList result;
    for (const QString &record : records) {
        if (hit.match(record).hasMatch())
            continue;
        const QRegularExpressionMatch match = count.match(record);
        if (!match.hasMatch())
            result.append(record);
        else if (match.hasCaptured(1))
            result.append(match.captured(1));
        else
            result.append(u"FNDA:"_s + match.captured(2));
    }
    return result;
}

// The pid in the helper's "ready <pid>" line, or 0.
qint64 readyPid(const QByteArray &output)
{
    static const QRegularExpression ready(QStringLiteral("ready (\\d+)"));
    return ready.match(QString::fromUtf8(output)).captured(1).toLongLong();
}

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll())
                                                            : QString();
}

} // namespace

class tst_QmlCoverageTool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void coversAProgramThatExits();
    void coversAQuickTest();
    void stopsALongRunningProgramOnSigterm();
    void failsWithoutReports();
    void failsWhenTheProgramCannotStart();
    void writesCoverageInfoByDefault();
    void printsItsVersion();
    void printsItsOptions();
    void failsWithoutAProgram();
    void failsWithoutATemporaryDirectory();
    void failsOnAnUnwritableOutput();
    void passesOnTheExitCodeOfTheProgram();
    void reportsAProgramThatCrashed();
    void writesQrcSourcesToTheSourcesDirectory();
    void stopsOnSigintToItsProcessGroupOnly();
    void stopsAProgramWithoutQmlAtOnce();
    void stopsAProgramThatNeverAnswers();
    void waitsForAProgramThatAnswersLate();
    void takesTheProgramAlongWhenKilled();
    void writesZerosForFilesTheProgramNeverLoads();
    void includesTheLinesTheEngineReports_data();
    void includesTheLinesTheEngineReports();
    void leavesLoadedFilesAsTheProgramReportsThem();
    void includesOnlyQmlAndJavaScript();
    void includesAFileLoadedThroughAnotherPath();
    void skipsIncludedFilesThatDoNotCompile();
    void failsOnAMissingIncludeDirectory();

private:
    int runTool(const QStringList &arguments, const QStringList &environment = { });
    QString helper() const;
    QString includeDir() const { return QFINDTESTDATA("data/include"); }
    QString lcov() const { return readAll(m_output); }

    QString m_binDir;
    QString m_tool;
    QTemporaryDir m_dir;
    QString m_output;
    QString m_toolOutput;
};

void tst_QmlCoverageTool::initTestCase()
{
    m_binDir = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    m_tool = m_binDir + u"/qmlcoverage"_s;
    QVERIFY(m_dir.isValid());
}

void tst_QmlCoverageTool::init()
{
    m_output = m_dir.filePath(QLatin1StringView(QTest::currentTestFunction()) + u".info"_s);
    m_toolOutput.clear();
}

int tst_QmlCoverageTool::runTool(const QStringList &arguments, const QStringList &environment)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setEnvironment(QProcess::systemEnvironment() + environment);
    process.start(m_tool, QStringList({ u"-o"_s, m_output }) + arguments);
    if (!process.waitForFinished())
        return -1;
    m_toolOutput = QString::fromUtf8(process.readAll());
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
}

QString tst_QmlCoverageTool::helper() const
{
    return QCoreApplication::applicationDirPath() + u"/qmlcoveragetoolhelper"_s;
}

void tst_QmlCoverageTool::coversAProgramThatExits()
{
    const QString qml = QFINDTESTDATA("data/exit.qml");
    QCOMPARE(runTool({ m_binDir + u"/qml"_s, qml }), 0);
    // exit.qml, util.js, and the configuration the qml tool loads in an engine of its own.
    QVERIFY(m_toolOutput.contains(u"3 file(s) -> "_s + m_output));
    QCOMPARE(lcovRecords(lcov(), qml),
             QStringList({ u"FN:5,add"_s,     u"FN:6,neverCalled"_s,
                           u"FN:7,sumTo"_s,   u"FN:13,expression for onCompleted"_s,
                           u"FNDA:3,add"_s,   u"FNDA:0,neverCalled"_s,
                           u"FNDA:1,sumTo"_s, u"FNDA:1,expression for onCompleted"_s,
                           u"FNF:4"_s,        u"FNH:3"_s,
                           u"DA:5,3"_s,       u"DA:6,0"_s,
                           u"DA:7,1"_s,       u"DA:8,1"_s,
                           u"DA:9,1"_s,       u"DA:10,3"_s,
                           u"DA:11,1"_s,      u"DA:13,1"_s,
                           u"DA:14,1"_s,      u"DA:15,1"_s,
                           u"DA:16,1"_s,
                           u"LF:11"_s,        u"LH:10"_s }));
    // A library's top-level code is a function too, on its first line.
    QCOMPARE(lcovRecords(lcov(), QFINDTESTDATA("data/util.js")),
             QStringList({ u"FN:1,%entry"_s, u"FN:1,three"_s, u"FNDA:1,%entry"_s, u"FNDA:1,three"_s,
                           u"FNF:2"_s, u"FNH:2"_s, u"DA:1,2"_s, u"DA:2,1"_s, u"LF:2"_s,
                           u"LH:2"_s }));
}

void tst_QmlCoverageTool::coversAQuickTest()
{
    const QString runner = m_binDir + u"/qmltestrunner"_s;
    if (!QFileInfo::exists(runner) && !QFileInfo::exists(runner + u".exe"_s))
        QSKIP("qmltestrunner is not built");
    const QString test = QFINDTESTDATA("data/tst_calc.qml");
    QCOMPARE(runTool({ runner, u"-platform"_s, u"offscreen"_s, u"-input"_s, test }), 0);
    QVERIFY(lcovRecords(lcov(), test).contains(u"FNDA:1,twice"_s));
}

void tst_QmlCoverageTool::stopsALongRunningProgramOnSigterm()
{
#ifndef Q_OS_UNIX
    QSKIP("qmlcoverage stops a program on a signal on Unix only");
#else
    const QString qml = QFINDTESTDATA("data/running.qml");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool, { u"-o"_s, m_output, m_binDir + u"/qml"_s, qml });
    QByteArray output; // QTRY_VERIFY evaluates more than once, and reading consumes
    QTRY_VERIFY_WITH_TIMEOUT((output += process.readAll()).contains("ready"), 30000);
    process.terminate();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(lcovRecords(lcov(), qml).contains(u"FNDA:1,once"_s));
#endif
}

void tst_QmlCoverageTool::failsWithoutReports()
{
    // A program without QML: it never answers, so there is nothing to report.
    QCOMPARE(runTool({ QCoreApplication::applicationFilePath(), u"-functions"_s }), 4);
    QVERIFY(m_toolOutput.contains(u"0 coverage report(s), 0 malformed"_s));
    QVERIFY(!QFileInfo::exists(m_output));
}

void tst_QmlCoverageTool::failsWhenTheProgramCannotStart()
{
    QCOMPARE(runTool({ m_dir.filePath(u"no-such-program"_s) }), 2);
    QVERIFY(m_toolOutput.contains(u"qmlcoverage: cannot run"_s));
}

void tst_QmlCoverageTool::writesCoverageInfoByDefault()
{
    const QString dir = m_dir.filePath(u"default"_s);
    QVERIFY(QDir().mkpath(dir));
    QProcess process;
    process.setWorkingDirectory(dir);
    process.start(m_tool, { m_binDir + u"/qml"_s, QFINDTESTDATA("data/exit.qml") });
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(QFileInfo::exists(dir + u"/coverage.info"_s));
}

void tst_QmlCoverageTool::printsItsVersion()
{
    QCOMPARE(runTool({ u"--version"_s }), 0);
    QVERIFY(m_toolOutput.contains(u"qmlcoverage " QT_VERSION_STR ""_s));
}

void tst_QmlCoverageTool::printsItsOptions()
{
    QCOMPARE(runTool({ u"--help"_s }), 0);
    QVERIFY(m_toolOutput.contains(u"--output <file>"_s));
    QVERIFY(m_toolOutput.contains(u"--sources <dir>"_s));
    QVERIFY(m_toolOutput.contains(u"[args...]"_s));
    QVERIFY(m_toolOutput.contains(u"\n  program "_s));
}

void tst_QmlCoverageTool::failsWithoutAProgram()
{
    QCOMPARE(runTool({ }), 2);
    QVERIFY(m_toolOutput.contains(u"Usage:"_s));
}

void tst_QmlCoverageTool::failsWithoutATemporaryDirectory()
{
#ifndef Q_OS_UNIX
    QSKIP("TMPDIR names the temporary directory on Unix only");
#else
    QCOMPARE(runTool({ m_binDir + u"/qml"_s, QFINDTESTDATA("data/exit.qml") },
                     { u"TMPDIR="_s + m_dir.filePath(u"missing"_s) }),
             2);
    QVERIFY(m_toolOutput.contains(u"cannot create a directory for the socket"_s));
#endif
}

void tst_QmlCoverageTool::failsOnAnUnwritableOutput()
{
    m_output = m_dir.filePath(u"missing/dir/report.info"_s);
    QCOMPARE(runTool({ m_binDir + u"/qml"_s, QFINDTESTDATA("data/exit.qml") }), 2);
    QVERIFY(m_toolOutput.contains(u"qmlcoverage: cannot write "_s + m_output));
}

void tst_QmlCoverageTool::passesOnTheExitCodeOfTheProgram()
{
    QCOMPARE(runTool({ m_binDir + u"/qml"_s, QFINDTESTDATA("data/exitcode.qml") }), 7);
    QVERIFY(QFileInfo::exists(m_output));
}

// SIGKILL: a crash that leaves no core dump behind.
void tst_QmlCoverageTool::reportsAProgramThatCrashed()
{
#ifndef Q_OS_UNIX
    QSKIP("The test kills the program with a Unix signal");
#else
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool, { u"-o"_s, m_output, helper() });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT(readyPid(output += process.readAll()), 30000);
    QCOMPARE(::kill(pid_t(readyPid(output)), SIGKILL), 0);
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 3);
    QVERIFY(process.readAll().contains("crashed; no coverage written"));
#endif
}

void tst_QmlCoverageTool::writesQrcSourcesToTheSourcesDirectory()
{
#ifndef Q_OS_UNIX
    QSKIP("qmlcoverage stops a program on a signal on Unix only");
#else
    const QString sources = m_dir.filePath(u"sources"_s);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool, { u"-o"_s, m_output, u"--sources"_s, sources, helper(), u"qml"_s });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT((output += process.readAll()).contains("ready"), 30000);
    process.terminate();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(lcovRecords(lcov(), sources + u"/qrc/resource.qml"_s).contains(u"FNDA:1,once"_s));
#endif
}

// A terminal's Ctrl+C goes to the whole process group: the program must not see it before the
// tool has its coverage.
void tst_QmlCoverageTool::stopsOnSigintToItsProcessGroupOnly()
{
#ifndef Q_OS_UNIX
    QSKIP("Process groups and SIGINT are Unix");
#else
    const QString qml = QFINDTESTDATA("data/running.qml");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
    process.start(m_tool, { u"-o"_s, m_output, m_binDir + u"/qml"_s, qml });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT((output += process.readAll()).contains("ready"), 30000);
    QCOMPARE(::kill(-pid_t(process.processId()), SIGINT), 0);
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(lcovRecords(lcov(), qml).contains(u"FNDA:1,once"_s));
#endif
}

// With no client connected there is nobody to ask: the tool stops the program instead of waiting
// for answers.
void tst_QmlCoverageTool::stopsAProgramWithoutQmlAtOnce()
{
#ifndef Q_OS_UNIX
    QSKIP("qmlcoverage stops a program on a signal on Unix only");
#else
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool, { u"-o"_s, m_output, helper() });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT((output += process.readAll()).contains("ready"), 30000);
    process.terminate();
    QVERIFY(process.waitForFinished(5000)); // well before the snapshot would time out
    QCOMPARE(process.exitCode(), 4);
#endif
}

// hanging.qml blocks its engine's thread, so the tool waits its full timeout of 10 s. The qml
// tool's configuration engine went away earlier, so there is coverage to write, but not all of it.
void tst_QmlCoverageTool::stopsAProgramThatNeverAnswers()
{
#ifndef Q_OS_UNIX
    QSKIP("qmlcoverage stops a program on a signal on Unix only");
#else
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool,
                  { u"-o"_s, m_output, m_binDir + u"/qml"_s, QFINDTESTDATA("data/hanging.qml") });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT((output += process.readAll()).contains("ready"), 30000);
    process.terminate();
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 5);
    QVERIFY(process.readAll().contains("not every engine answered the final snapshot"));
    QVERIFY(QFileInfo::exists(m_output));
#endif
}

// busy.qml answers 2 s late: well within the timeout, so the coverage is complete.
void tst_QmlCoverageTool::waitsForAProgramThatAnswersLate()
{
#ifndef Q_OS_UNIX
    QSKIP("qmlcoverage stops a program on a signal on Unix only");
#else
    const QString qml = QFINDTESTDATA("data/busy.qml");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool, { u"-o"_s, m_output, m_binDir + u"/qml"_s, qml });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT((output += process.readAll()).contains("ready"), 30000);
    process.terminate();
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(lcovRecords(lcov(), qml).contains(u"FNDA:1,once"_s));
#endif
}

void tst_QmlCoverageTool::takesTheProgramAlongWhenKilled()
{
#ifndef Q_OS_LINUX
    QSKIP("The program learns of the tool's death through a Linux prctl()");
#else
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_tool, { u"-o"_s, m_output, helper() });
    QByteArray output;
    QTRY_VERIFY_WITH_TIMEOUT(readyPid(output += process.readAll()), 30000);
    process.kill();
    QVERIFY(process.waitForFinished());
    QTRY_COMPARE(::kill(pid_t(readyPid(output)), 0), -1);
#endif
}


// none.qml loads none of the other files below include/.
void tst_QmlCoverageTool::writesZerosForFilesTheProgramNeverLoads()
{
    QCOMPARE(runTool({ u"--include"_s, includeDir(), m_binDir + u"/qml"_s,
                       includeDir() + u"/none.qml"_s }),
             0);
    QCOMPARE(lcovRecords(lcov(), includeDir() + u"/lib.js"_s),
             QStringList({ u"FN:1,%entry"_s, u"FN:1,used"_s, u"FN:5,unused"_s,
                           u"FNDA:0,%entry"_s, u"FNDA:0,used"_s, u"FNDA:0,unused"_s,
                           u"FNF:3"_s, u"FNH:0"_s,
                           u"DA:1,0"_s, u"DA:2,0"_s, u"DA:5,0"_s, u"DA:6,0"_s, u"DA:7,0"_s,
                           u"DA:8,0"_s, u"LF:6"_s, u"LH:0"_s }));
}

void tst_QmlCoverageTool::includesTheLinesTheEngineReports_data()
{
    QTest::addColumn<QString>("file");
    QTest::newRow("JavaScript library") << u"lib.js"_s;
    QTest::newRow("JavaScript module") << u"module.mjs"_s;
    QTest::newRow("QML component") << u"Widget.qml"_s;
    QTest::newRow("QML document") << u"all.qml"_s;
}

// all.qml loads every other file below include/, none.qml none of them.
void tst_QmlCoverageTool::includesTheLinesTheEngineReports()
{
    QFETCH(QString, file);
    const QString path = includeDir() + u'/' + file;
    QCOMPARE(runTool({ m_binDir + u"/qml"_s, includeDir() + u"/all.qml"_s }), 0);
    const QStringList loaded = lcovRecords(lcov(), path);
    QVERIFY(!loaded.isEmpty());

    QCOMPARE(runTool({ u"--include"_s, includeDir(), m_binDir + u"/qml"_s,
                       includeDir() + u"/none.qml"_s }),
             0);
    const QStringList included = lcovRecords(lcov(), path);
    QCOMPARE(withoutCounts(included), withoutCounts(loaded));
    QVERIFY(included.contains(u"LH:0"_s));
    QVERIFY(included.contains(u"FNH:0"_s));
}

void tst_QmlCoverageTool::leavesLoadedFilesAsTheProgramReportsThem()
{
    const QStringList program({ m_binDir + u"/qml"_s, includeDir() + u"/all.qml"_s });
    QCOMPARE(runTool(program), 0);
    const QString alone = lcov();

    QCOMPARE(runTool(QStringList({ u"--include"_s, includeDir() }) + program), 0);
    QCOMPARE(lcovRecords(lcov(), includeDir() + u"/all.qml"_s),
             lcovRecords(alone, includeDir() + u"/all.qml"_s));
    QCOMPARE(lcovRecords(lcov(), includeDir() + u"/Widget.qml"_s),
             lcovRecords(alone, includeDir() + u"/Widget.qml"_s));
    QCOMPARE(lcovRecords(lcov(), includeDir() + u"/lib.js"_s),
             lcovRecords(alone, includeDir() + u"/lib.js"_s));
    QCOMPARE(lcovRecords(lcov(), includeDir() + u"/module.mjs"_s),
             lcovRecords(alone, includeDir() + u"/module.mjs"_s));
    QCOMPARE(lcov().count(u"SF:"_s + includeDir()), 5); // and none.qml, with zeros
}

void tst_QmlCoverageTool::includesOnlyQmlAndJavaScript()
{
    QCOMPARE(runTool({ u"--include"_s, includeDir(), m_binDir + u"/qml"_s,
                       includeDir() + u"/none.qml"_s }),
             0);
    QVERIFY(!lcov().contains(u"notes.txt"_s));
}

// The program loads all.qml through a symbolic link to include/, the tool reads include/ itself.
void tst_QmlCoverageTool::includesAFileLoadedThroughAnotherPath()
{
    const QString link = m_dir.filePath(u"link"_s);
    if (!QFile::exists(link) && !QFile::link(includeDir(), link))
        QSKIP("Cannot create a symbolic link");
    QCOMPARE(runTool({ u"--include"_s, includeDir(), m_binDir + u"/qml"_s,
                       link + u"/all.qml"_s }),
             0);
    QVERIFY(lcovRecords(lcov(), includeDir() + u"/all.qml"_s).isEmpty());
    QVERIFY(lcovRecords(lcov(), link + u"/all.qml"_s).contains(u"LH:5"_s));
}

void tst_QmlCoverageTool::skipsIncludedFilesThatDoNotCompile()
{
    const QString broken = QFINDTESTDATA("data/include-broken");
    QCOMPARE(runTool({ u"--include"_s, broken, m_binDir + u"/qml"_s,
                       includeDir() + u"/none.qml"_s }),
             0);
    QVERIFY(m_toolOutput.contains(u"qmlcoverage: cannot compile "_s + broken
                                  + u"/broken.qml"_s));
    QVERIFY(!lcov().contains(u"broken.qml"_s));
}

void tst_QmlCoverageTool::failsOnAMissingIncludeDirectory()
{
    const QString missing = m_dir.filePath(u"missing"_s);
    QCOMPARE(runTool({ u"--include"_s, missing, m_binDir + u"/qml"_s,
                       includeDir() + u"/none.qml"_s }),
             2);
    QVERIFY(m_toolOutput.contains(u"qmlcoverage: no directory "_s + missing));
}

QTEST_GUILESS_MAIN(tst_QmlCoverageTool)

#include "tst_qmlcoveragetool.moc"
