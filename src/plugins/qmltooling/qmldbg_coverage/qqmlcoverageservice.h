// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant

#ifndef QQMLCOVERAGESERVICE_H
#define QQMLCOVERAGESERVICE_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <private/qqmldebugservice_p.h>

#include <QtCore/qhash.h>
#include <QtCore/qlist.h>
#include <QtCore/qmutex.h>

QT_BEGIN_NAMESPACE

class QQmlCoverageCollector;

// Records which lines and functions of QML and JavaScript code each engine executes.
//
// It gives every engine a QV4::Debugging::Debugger that never pauses. With a debugger attached,
// the compiler emits a Debug instruction at each line change, the JIT is not used, and AOT
// compiled and disk cached units are bypassed, so every executed line reaches the debugger.
// Whether code gets Debug instructions is decided when the engine is set up, so only engines
// created while a client is connected are covered: start the program with "block". An engine
// has room for one debugger, and the QML debugger wins: with the V8Debugger or NativeQmlDebugger
// service loaded, nothing is covered.
//
// Protocol (QQmlDebugPacket):
//   client -> service: "SNAPSHOT" quint32 round       (round > 0)
//   service -> client: "REPORT" quint32 round qint32 roundSize qint32 fileCount
//                      fileCount x (QString url, QString source,
//                          qint32 functionCount x (QString name, qint32 line, quint64 calls),
//                          qint32 lineCount x (qint32 line, quint64 hits))
// Every engine asked answers a snapshot request once, with that round and the number of engines
// asked (roundSize): with a snapshot, or with its final report if it is removed first. Every
// engine reports when it is removed, with round 0 if it owes no answer. A report holds the hits
// since the engine's previous report, so a client sums them. A report lists every executable line
// of each loaded unit, unexecuted ones with 0. source is only set for qrc: units, once per engine.
class QQmlCoverageServiceImpl : public QQmlDebugService
{
    Q_OBJECT
public:
    static const QString s_key;

    explicit QQmlCoverageServiceImpl(QObject *parent = nullptr);

    void engineAboutToBeAdded(QJSEngine *engine) override;
    void engineAboutToBeRemoved(QJSEngine *engine) override;
    void messageReceived(const QByteArray &message) override;

private:
    struct Round
    {
        quint32 round = 0;
        qint32 size = 0;
    };

    // m_collectors: engine threads add and remove, the server thread reads. m_owed: the server
    // thread adds, engine threads remove.
    QMutex m_mutex;
    QList<QQmlCoverageCollector *> m_collectors;
    QHash<QQmlCoverageCollector *, Round> m_owed; // the latest snapshot request not yet answered
};

QT_END_NAMESPACE

#endif // QQMLCOVERAGESERVICE_H
