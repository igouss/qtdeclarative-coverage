// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlcoveragereport.h"

#include <private/qpacket_p.h>

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qset.h>
#include <QtCore/qtextstream.h>
#include <QtCore/qurl.h>

QT_BEGIN_NAMESPACE

namespace QmlCoverage {

namespace {

// Counts come from the program: elements are read one at a time, never reserved up front, so a
// corrupt count ends in a read past the end instead of a huge allocation.
bool readCount(QPacket &packet, qint32 *count)
{
    packet >> *count;
    return packet.status() == QDataStream::Ok && *count >= 0;
}

bool readFile(QPacket &packet, Files *files)
{
    QString url;
    File file;
    qint32 functionCount = 0;
    packet >> url >> file.source;
    if (!readCount(packet, &functionCount))
        return false;
    for (qint32 i = 0; i < functionCount && packet.status() == QDataStream::Ok; ++i) {
        QString name;
        qint32 line = 0;
        quint64 calls = 0;
        packet >> name >> line >> calls;
        file.functions[{ line, name }] += calls;
    }
    qint32 lineCount = 0;
    if (!readCount(packet, &lineCount))
        return false;
    for (qint32 i = 0; i < lineCount && packet.status() == QDataStream::Ok; ++i) {
        qint32 line = 0;
        quint64 hits = 0;
        packet >> line >> hits;
        file.lines.insert(line, hits);
    }
    files->insert(url, file);
    return packet.status() == QDataStream::Ok;
}

// lcov separates the function name with a comma and ends it at the line break. A name must be
// unique in its file, and QML names its bindings after their property: "expression for target".
QString lcovFunctionName(QString name, int line, const QSet<QString> &shared)
{
    if (name.isEmpty())
        name = QStringLiteral("<anonymous>:%1").arg(line);
    else if (shared.contains(name))
        name += QStringLiteral(":%1").arg(line);
    return name.replace(QLatin1Char(','), QLatin1Char(';'))
            .replace(QLatin1Char('\n'), QLatin1Char(' '));
}

bool writeText(const QString &path, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    QFile out(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && out.open(QIODevice::WriteOnly)
            && out.write(bytes) == bytes.size() && out.flush();
}

} // namespace

std::optional<Report> decodeReport(const QByteArray &message, int dataStreamVersion)
{
    QPacket packet(dataStreamVersion, message);
    QByteArray command;
    Report report;
    qint32 fileCount = 0;
    packet >> command >> report.round >> report.roundSize;
    if (command != "REPORT" || !readCount(packet, &fileCount))
        return std::nullopt;
    for (qint32 i = 0; i < fileCount; ++i) {
        if (!readFile(packet, &report.files))
            return std::nullopt;
    }
    if (!packet.atEnd())
        return std::nullopt;
    return report;
}

void merge(Files *totals, const Files &report)
{
    for (auto file = report.cbegin(); file != report.cend(); ++file) {
        File &total = (*totals)[file.key()];
        if (total.source.isEmpty())
            total.source = file->source;
        for (auto it = file->lines.cbegin(); it != file->lines.cend(); ++it)
            total.lines[it.key()] += it.value();
        for (auto it = file->functions.cbegin(); it != file->functions.cend(); ++it)
            total.functions[it.key()] += it.value();
    }
}

quint32 Collector::startSnapshot()
{
    m_waiting = true;
    m_answered = 0;
    return ++m_round;
}

bool Collector::receive(const QByteArray &message, int dataStreamVersion)
{
    const std::optional<Report> report = decodeReport(message, dataStreamVersion);
    if (!report) {
        ++m_malformed;
        return false;
    }
    ++m_reports;
    merge(&m_files, report->files);
    if (!m_waiting || report->round != m_round)
        return false;
    // With no engine left, the service answers once, for a round of size 0.
    if (++m_answered < report->roundSize)
        return false;
    m_waiting = false;
    return true;
}

bool writeLcov(const Files &files, const QString &output, const QString &sourceDir,
               QStringList *skipped, QString *failedPath)
{
    *failedPath = output;
    QFile out(output);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream s(&out);
    for (auto file = files.cbegin(); file != files.cend(); ++file) {
        const QUrl url(file.key());
        QString path;
        if (url.isLocalFile()) {
            path = url.toLocalFile();
        } else if (!file->source.isEmpty()) {
            path = QDir(sourceDir).absoluteFilePath(url.scheme() + url.path());
            if (!writeText(path, file->source)) {
                *failedPath = path;
                return false;
            }
        } else {
            skipped->append(file.key());
            continue;
        }
        s << "TN:\nSF:" << path << '\n';
        QSet<QString> named;
        QSet<QString> shared;
        for (auto f = file->functions.cbegin(); f != file->functions.cend(); ++f) {
            if (named.contains(f.key().second))
                shared.insert(f.key().second);
            named.insert(f.key().second);
        }
        for (auto f = file->functions.cbegin(); f != file->functions.cend(); ++f)
            s << "FN:" << f.key().first << ','
              << lcovFunctionName(f.key().second, f.key().first, shared) << '\n';
        int functionsHit = 0;
        for (auto f = file->functions.cbegin(); f != file->functions.cend(); ++f) {
            s << "FNDA:" << f.value() << ','
              << lcovFunctionName(f.key().second, f.key().first, shared) << '\n';
            if (f.value() > 0)
                ++functionsHit;
        }
        s << "FNF:" << file->functions.size() << "\nFNH:" << functionsHit << '\n';
        int linesFound = 0;
        int linesHit = 0;
        for (auto l = file->lines.cbegin(); l != file->lines.cend(); ++l) {
            s << "DA:" << l.key() << ',' << l.value() << '\n';
            ++linesFound;
            if (l.value() > 0)
                ++linesHit;
        }
        s << "LF:" << linesFound << "\nLH:" << linesHit << "\nend_of_record\n";
    }
    s.flush(); // writes and flushes the file: a full disk shows only now
    return s.status() == QTextStream::Ok;
}

} // namespace QmlCoverage

QT_END_NAMESPACE
