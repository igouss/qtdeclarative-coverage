// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlcoverageinclude.h"

#include <private/qqmlirbuilder_p.h>
#include <private/qqmljscompiler_p.h>
#include <private/qv4compileddata_p.h>

#include <QtCore/qdiriterator.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qset.h>
#include <QtCore/qurl.h>

#include <optional>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

namespace QmlCoverage {

namespace {

using QV4::Compiler::CompilationMode;

// The lines and functions of a unit, as the CodeCoverage service reports them.
File unexecuted(const QV4::CompiledData::Unit *unit)
{
    File file;
    for (uint i = 0; i < unit->functionTableSize; ++i) {
        const QV4::CompiledData::Function *function = unit->functionAt(int(i));
        // While the unit is saved, its strings point into it, and it goes away after.
        const QString name = unit->stringAtInternal(function->nameIndex);
        file.functions.insert(
                { int(function->location.line()), QString(name.constData(), name.size()) }, 0);
        const QV4::CompiledData::CodeOffsetToLineAndStatement *table =
                function->lineAndStatementNumberTable();
        for (quint16 j = 0; j < function->nLineAndStatementNumbers; ++j) {
            if (table[j].line > 0)
                file.lines.insert(table[j].line, 0);
        }
    }
    return file;
}

// With the service attached, the engine compiles in Debug mode, and the line table follows it.
std::optional<File> compile(const QString &path, QString *error)
{
    std::optional<File> file;
    const QQmlJSSaveFunction keep = [&file](const QV4::CompiledData::SaveableUnitPointer &unit,
                                            const QQmlJSAotFunctionMap &,
                                            const LookupSignatures &, QString *) {
        return unit.saveToDisk<char>([&file](const char *data, quint32) {
            file = unexecuted(reinterpret_cast<const QV4::CompiledData::Unit *>(data));
            return true;
        });
    };
    QQmlJSCompileError compileError;
    bool compiled = false;
    if (path.endsWith(u".qml"_s)) {
        QmlIR::Document document(QString(), QString(), CompilationMode::Coverage);
        compiled = qCompileQmlFile(document, path, keep, nullptr, &compileError);
    } else {
        compiled = qCompileJSFile(path, QString(), keep, &compileError, CompilationMode::Coverage);
    }
    if (!compiled) {
        *error = compileError.message;
        return std::nullopt;
    }
    return file;
}

} // namespace

void includeUnloaded(Files *files, const QString &dir, QStringList *failures)
{
    // The program may have loaded a file through another path to it, a symbolic link for one.
    QSet<QString> loaded;
    for (auto it = files->cbegin(); it != files->cend(); ++it) {
        const QUrl url(it.key());
        if (url.isLocalFile())
            loaded.insert(QFileInfo(url.toLocalFile()).canonicalFilePath());
    }

    QDirIterator it(dir, { u"*.qml"_s, u"*.js"_s, u"*.mjs"_s }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        if (loaded.contains(info.canonicalFilePath()))
            continue;
        QString error;
        if (const std::optional<File> file = compile(info.absoluteFilePath(), &error))
            files->insert(QUrl::fromLocalFile(info.absoluteFilePath()).toString(), *file);
        else
            failures->append(info.absoluteFilePath() + u": "_s + error);
    }
}

} // namespace QmlCoverage

QT_END_NAMESPACE
