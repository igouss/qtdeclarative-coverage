// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef QMLCOVERAGEINCLUDE_H
#define QMLCOVERAGEINCLUDE_H

#include "qmlcoveragereport.h"

#include <QtCore/qstringlist.h>

QT_BEGIN_NAMESPACE

namespace QmlCoverage {

// Adds each .qml, .js and .mjs file below dir that files does not hold yet, with every line and
// function at zero. To find those, it compiles the file as the engine does for the CodeCoverage
// service. A file that does not compile is left out and described in failures.
void includeUnloaded(Files *files, const QString &dir, QStringList *failures);

} // namespace QmlCoverage

QT_END_NAMESPACE

#endif // QMLCOVERAGEINCLUDE_H
