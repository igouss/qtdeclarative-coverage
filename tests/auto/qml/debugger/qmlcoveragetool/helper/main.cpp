// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

// Test program for tst_qmlcoveragetool:
//
//   qmlcoveragetoolhelper -qmljsdebugger=... [qml]
//
// Prints "ready <pid>" and runs until stopped. Without "qml", it is a program without QML: it never
// enables QML debugging, so it never connects. With "qml", it loads resource.qml from its
// resources, a file the tool cannot read from disk.

#include <QtCore/qcoreapplication.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmldebug.h>
#include <QtQml/qqmlengine.h>

#include <cstring>
#include <memory>

int main(int argc, char *argv[])
{
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
