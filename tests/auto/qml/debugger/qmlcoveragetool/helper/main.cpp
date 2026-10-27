// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

// Test program for tst_qmlcoveragetool:
//
//   qmlcoveragetoolhelper -qmljsdebugger=... [qml]
//
// Prints "ready <pid>" and runs until stopped. Without "qml", it is a program without QML: it never
// enables QML debugging, so it never connects. With "qml", it loads resource.qml from its
// resources, a file the tool cannot read from disk.
//
//   qmlcoveragetoolhelper serve <port> <file>
//
// Starts a debug server of its own with the CodeCoverage service on <port>, as some programs do,
// and waits for a client. Then loads <file>, prints "ready <pid>" and runs until stopped or until
// the QML calls Qt.quit().

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdir.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmldebug.h>
#include <QtQml/qqmlengine.h>

#include <cstring>
#include <memory>

namespace {

int serve(int argc, char *argv[])
{
    QQmlDebuggingEnabler::enableDebugging(false);
    QCoreApplication app(argc, argv);
    QQmlDebuggingEnabler::setServices({ QStringLiteral("CodeCoverage") });
    if (!QQmlDebuggingEnabler::startTcpDebugServer(QString::fromLocal8Bit(argv[2]).toInt(),
                                                   QQmlDebuggingEnabler::WaitForClient)) {
        qFatal("cannot start the debug server");
    }
    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit);
    const QString file = QString::fromLocal8Bit(argv[3]);
    QQmlComponent component(&engine, QUrl::fromUserInput(file, QDir::currentPath()));
    const std::unique_ptr<QObject> root(component.create());
    if (!root)
        qFatal("%s", qPrintable(component.errorString()));
    qInfo("ready %lld", QCoreApplication::applicationPid());
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc == 4 && std::strcmp(argv[1], "serve") == 0)
        return serve(argc, argv);

    const bool qml = argc > 1 && std::strcmp(argv[argc - 1], "qml") == 0;
    if (qml)
        QQmlDebuggingEnabler::enableDebugging(false);
    QCoreApplication app(argc, argv);
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    if (qml) {
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/resource.qml")));
        root.reset(component.create());
        if (!root)
            qFatal("%s", qPrintable(component.errorString()));
    }
    qInfo("ready %lld", QCoreApplication::applicationPid());
    return app.exec();
}
