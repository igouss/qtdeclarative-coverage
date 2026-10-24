// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef QMLCOVERAGEREPORT_H
#define QMLCOVERAGEREPORT_H

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qmap.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>

#include <optional>
#include <utility>

QT_BEGIN_NAMESPACE

namespace QmlCoverage {

// One file of one engine's report, or the sum of all reports that mentioned it.
struct File
{
    QString source; // set only for files the tool cannot read itself (qrc:)
    QMap<int, quint64> lines; // every executable line, including those never hit
    QMap<std::pair<int, QString>, quint64> functions; // (line, name) -> calls
};

using Files = QMap<QString, File>; // by URL

struct Report
{
    quint32 round = 0; // the snapshot request answered, or 0 for a final report that answers none
    qint32 roundSize = 0; // engines asked to answer that request
    Files files;
};

// Reads a message of the CodeCoverage service; see qqmlcoverageservice.h for the format.
// Returns nothing for a message that does not hold exactly one well-formed report.
std::optional<Report> decodeReport(const QByteArray &message, int dataStreamVersion);

// Adds the hits of a report to the totals.
void merge(Files *totals, const Files &report);

// Sums the reports of a run, and tells when every engine asked has answered a snapshot request.
class Collector
{
public:
    // Starts a snapshot request, and returns the round to send with it.
    quint32 startSnapshot();

    // Takes a message of the service. Returns true when it completes the current snapshot request.
    bool receive(const QByteArray &message, int dataStreamVersion);

    int reports() const { return m_reports; }
    int malformed() const { return m_malformed; }
    const Files &files() const { return m_files; }

private:
    Files m_files;
    int m_reports = 0;
    int m_malformed = 0;
    quint32 m_round = 0; // the latest snapshot request
    bool m_waiting = false; // for answers to it
    qint32 m_answered = 0;
};

// Writes the totals as lcov. A record points at the original of a file: URL, and at a copy below
// sourceDir of a source the program sent (qrc:). Files with neither are listed in skipped. On
// failure, failedPath is the file that could not be written.
bool writeLcov(const Files &files, const QString &output, const QString &sourceDir,
               QStringList *skipped, QString *failedPath);

} // namespace QmlCoverage

QT_END_NAMESPACE

#endif // QMLCOVERAGEREPORT_H
