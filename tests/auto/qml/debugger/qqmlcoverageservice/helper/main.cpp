// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

// Test program for tst_qqmlcoverageservice:
//
//   qqmlcoverageservicehelper -qmljsdebugger=... <file or qrc: URL>[,<file or qrc: URL>...]
//
// Loads each QML in an engine of its own and runs until killed. With COVERAGE_HELPER_RELEASE set,
// the last engine is destroyed once its QML is loaded, and the program keeps running without it.
// With COVERAGE_HELPER_STALL=<ms> as well, the program prints "stalling" and blocks its thread for
// that long before, so that requests arriving meanwhile find the engine gone when they get to run.
// With COVERAGE_HELPER_RELEASE_AFTER=<ms>, the last engine is destroyed after that long instead,
// from the event loop, so that requests arriving before have run.
// Prints whether each engine has a debugger attached.

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdir.h>
#include <QtCore/qthread.h>
#include <QtCore/qtimer.h>
#include <QtCore/qtenvironmentvariables.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmldebug.h>
#include <QtQml/qqmlengine.h>
#include <private/qv4engine_p.h>

#include <memory>
#include <vector>

int main(int argc, char *argv[])
{
    QQmlDebuggingEnabler::enableDebugging(false);
    QCoreApplication app(argc, argv);
    // One argument: the test harness passes its program arguments as a single string.
    const QStringList files = app.arguments().last().split(QLatin1Char(','));

    std::vector<std::unique_ptr<QQmlEngine>> engines;
    std::vector<std::unique_ptr<QObject>> roots;
    for (const QString &file : files) {
        engines.push_back(std::make_unique<QQmlEngine>());
        QQmlComponent component(engines.back().get(),
                                QUrl::fromUserInput(file, QDir::currentPath()));
        roots.emplace_back(component.create());
        if (!roots.back())
            qFatal("%s", qPrintable(component.errorString()));
        qInfo("%s: debugger %s", qPrintable(file),
              engines.back()->handle()->debugger() ? "attached" : "none");
    }
    const auto release = [&] {
        roots.pop_back();
        engines.pop_back();
    };
    if (qEnvironmentVariableIsSet("COVERAGE_HELPER_RELEASE")) {
        if (const int stall = qEnvironmentVariableIntValue("COVERAGE_HELPER_STALL")) {
            qInfo("stalling");
            QThread::msleep(stall);
        }
        release();
    }
    if (const int after = qEnvironmentVariableIntValue("COVERAGE_HELPER_RELEASE_AFTER"))
        QTimer::singleShot(after, release);
    return app.exec();
}
