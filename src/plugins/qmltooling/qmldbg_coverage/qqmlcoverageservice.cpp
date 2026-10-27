// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant

#include "qqmlcoverageservice.h"

#include <private/qqmldebugconnector_p.h>
#include <private/qqmldebugserviceinterfaces_p.h>
#include <private/qv4debugging_p.h>
#include <private/qv4engine_p.h>
#include <private/qv4executablecompilationunit_p.h>
#include <private/qv4function_p.h>
#include <private/qv4stackframe_p.h>
#include <private/qversionedpacket_p.h>

#include <QtQml/qjsengine.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qfile.h>
#include <QtCore/qhash.h>
#include <QtCore/qmap.h>
#include <QtCore/qset.h>
#include <QtCore/qurl.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

using QQmlDebugPacket = QVersionedPacket<QQmlDebugConnector>;

const QString QQmlCoverageServiceImpl::s_key = QStringLiteral("CodeCoverage");

namespace {

struct FunctionHits
{
    quint64 calls = 0;
    QHash<int, quint64> lines;
};

using CompiledFunction = QV4::CompiledData::Function;
using UnitPtr = QQmlRefPointer<QV4::ExecutableCompilationUnit>;

// The client can read file: sources itself. Anything else it only sees if we send it.
QString sourceTheClientCannotRead(const QUrl &url)
{
    if (url.scheme() != QLatin1String("qrc"))
        return QString();
    QFile file(QLatin1Char(':') + url.path());
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

// The debugger services attach their debuggers after us, and an engine holds only one.
bool debuggerServiceLoaded()
{
    return QQmlDebugConnector::service<QV4DebugService>()
            || QQmlDebugConnector::service<QQmlNativeDebugService>();
}

} // namespace

class QQmlCoverageCollector : public QV4::Debugging::Debugger
{
public:
    explicit QQmlCoverageCollector(QJSEngine *engine)
        : m_jsEngine(engine), m_engine(engine->handle())
    {
    }

    QJSEngine *jsEngine() const { return m_jsEngine; }
    QV4::ExecutionEngine *engine() const { return m_engine; }

    // Debug instructions only reach maybeBreakAtInstruction() when this returns true.
    bool pauseAtNextOpportunity() const override { return true; }

    void maybeBreakAtInstruction() override
    {
        const QV4::CppStackFrame *frame = m_engine->currentStackFrame;
        const int line = frame->lineNumber();
        // The Debug instruction before a return carries the negated line number.
        if (line > 0)
            ++hitsFor(frame->v4Function).lines[line];
    }

    void enteringFunction() override { ++hitsFor(m_engine->currentStackFrame->v4Function).calls; }

    void leavingFunction(const QV4::ReturnedValue &) override { }
    void aboutToThrow() override { }

    // Hits since the previous report; resets the counts. Call on the engine's thread.
    QByteArray takeReport(quint32 round, qint32 roundSize);

private:
    FunctionHits &hitsFor(QV4::Function *function)
    {
        const auto it = m_hits.find(function->compiledFunction);
        if (it != m_hits.end())
            return *it;
        // Hold the unit, so the compiled function stays valid until it is reported.
        QV4::ExecutableCompilationUnit *unit = function->executableCompilationUnit();
        m_units.insert(unit, UnitPtr(unit));
        return m_hits[function->compiledFunction];
    }

    QJSEngine *m_jsEngine;
    QV4::ExecutionEngine *m_engine;
    QHash<const CompiledFunction *, FunctionHits> m_hits;
    QHash<const QV4::ExecutableCompilationUnit *, UnitPtr> m_units;
    QSet<const QV4::ExecutableCompilationUnit *> m_sourceSent;
};

QByteArray QQmlCoverageCollector::takeReport(quint32 round, qint32 roundSize)
{
    // Units the engine loaded but never ran still report their lines, with zero hits.
    QHash<const QV4::ExecutableCompilationUnit *, UnitPtr> units = m_units;
    const auto loaded = m_engine->compilationUnits();
    for (const UnitPtr &unit : loaded)
        units.insert(unit.data(), unit);

    QQmlDebugPacket packet;
    packet << QByteArray("REPORT") << round << roundSize << qint32(units.size());
    for (const UnitPtr &unit : std::as_const(units)) {
        QString source;
        if (!m_sourceSent.contains(unit.data())) {
            source = sourceTheClientCannotRead(unit->finalUrl());
            m_sourceSent.insert(unit.data());
        }
        packet << unit->finalUrlString() << source;

        const QV4::CompiledData::Unit *data = unit->unitData();
        QMap<int, quint64> lines;
        packet << qint32(data->functionTableSize);
        for (uint i = 0; i < data->functionTableSize; ++i) {
            const CompiledFunction *compiled = data->functionAt(int(i));
            const FunctionHits hits = m_hits.value(compiled);
            packet << unit->stringAt(compiled->nameIndex) << qint32(compiled->location.line())
                   << hits.calls;
            // The table has an entry per code offset, so a line usually appears several times.
            QSet<int> functionLines;
            const QV4::CompiledData::CodeOffsetToLineAndStatement *table =
                    compiled->lineAndStatementNumberTable();
            for (quint16 j = 0; j < compiled->nLineAndStatementNumbers; ++j) {
                if (table[j].line > 0)
                    functionLines.insert(table[j].line);
            }
            for (int line : std::as_const(functionLines))
                lines[line] += hits.lines.value(line);
        }
        packet << qint32(lines.size());
        for (auto it = lines.cbegin(); it != lines.cend(); ++it)
            packet << qint32(it.key()) << it.value();
    }
    m_hits.clear(); // the units stay held, so loaded-but-idle code keeps reporting zeros
    return packet.data();
}

QQmlCoverageServiceImpl::QQmlCoverageServiceImpl(QObject *parent)
    : QQmlDebugService(s_key, 1.0f, parent)
{
    // Post routines run in reverse order of registration: this one runs before the debug server
    // stops its thread, while reports can still reach the client.
    qAddPostRoutine(reportEnginesLeftAlive);
}

QQmlCoverageServiceImpl::~QQmlCoverageServiceImpl()
{
    qRemovePostRoutine(reportEnginesLeftAlive);
}

// Engines the program never destroys report as the application object goes away. It is destroyed
// on the main thread, which is where every engine known to the debug server lives.
void QQmlCoverageServiceImpl::reportEnginesLeftAlive()
{
    QQmlCoverageServiceImpl *service = QQmlDebugConnector::service<QQmlCoverageServiceImpl>();
    if (!service)
        return;
    QMutexLocker lock(&service->m_mutex);
    while (!service->m_collectors.isEmpty())
        service->sendFinalReport(service->m_collectors.constLast());
}

// Call with m_mutex locked.
void QQmlCoverageServiceImpl::sendFinalReport(QQmlCoverageCollector *collector)
{
    m_collectors.removeOne(collector);
    // Its answer to a pending snapshot request will never run; the final report is it.
    const Round owed = m_owed.take(collector);
    if (state() == Enabled)
        emit messageToClient(s_key, collector->takeReport(owed.round, owed.size));
}

void QQmlCoverageServiceImpl::engineAboutToBeAdded(QJSEngine *engine)
{
    QV4::ExecutionEngine *ee = engine ? engine->handle() : nullptr;
    if (ee && state() == Enabled && debuggerServiceLoaded()) {
        qWarning("CodeCoverage: a QML debugger service is loaded too; coverage disabled");
    } else if (ee && state() == Enabled && !ee->debugger()) {
        QQmlCoverageCollector *collector = new QQmlCoverageCollector(engine);
        ee->setDebugger(collector); // the engine owns it from here on
        QMutexLocker lock(&m_mutex);
        m_collectors.append(collector);
    }
    QQmlDebugService::engineAboutToBeAdded(engine);
}

void QQmlCoverageServiceImpl::engineAboutToBeRemoved(QJSEngine *engine)
{
    QV4::ExecutionEngine *ee = engine ? engine->handle() : nullptr;
    {
        QMutexLocker lock(&m_mutex);
        const auto it = std::find_if(m_collectors.cbegin(), m_collectors.cend(),
                                     [ee](const QQmlCoverageCollector *collector) {
                                         return collector->engine() == ee;
                                     });
        if (it != m_collectors.cend())
            sendFinalReport(*it);
    }
    QQmlDebugService::engineAboutToBeRemoved(engine);
}

// Runs on the debug server's thread. The counts belong to each engine's thread, so the report
// is taken there; an engine destroyed before it gets to run sends its final report instead.
void QQmlCoverageServiceImpl::messageReceived(const QByteArray &message)
{
    QQmlDebugPacket packet(message);
    QByteArray command;
    quint32 round = 0;
    packet >> command >> round;
    if (command != "SNAPSHOT" || round == 0 || !packet.atEnd())
        return;

    QMutexLocker lock(&m_mutex);
    const Round asked{ round, qint32(m_collectors.size()) };
    for (QQmlCoverageCollector *collector : std::as_const(m_collectors)) {
        m_owed.insert(collector, asked);
        QMetaObject::invokeMethod(
                collector->jsEngine(),
                [this, collector, asked] {
                    {
                        QMutexLocker lock(&m_mutex);
                        if (m_owed.value(collector).round == asked.round)
                            m_owed.remove(collector);
                    }
                    emit messageToClient(s_key, collector->takeReport(asked.round, asked.size));
                },
                Qt::QueuedConnection);
    }
    if (m_collectors.isEmpty()) {
        QQmlDebugPacket empty;
        empty << QByteArray("REPORT") << round << qint32(0) << qint32(0);
        emit messageToClient(s_key, empty.data());
    }
}

QT_END_NAMESPACE

#include "moc_qqmlcoverageservice.cpp"
