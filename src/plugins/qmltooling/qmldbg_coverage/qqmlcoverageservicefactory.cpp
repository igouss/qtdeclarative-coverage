// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant

#include "qqmlcoverageservicefactory.h"
#include "qqmlcoverageservice.h"

QT_BEGIN_NAMESPACE

QQmlDebugService *QQmlCoverageServiceFactory::create(const QString &key)
{
    if (key == QQmlCoverageServiceImpl::s_key)
        return new QQmlCoverageServiceImpl(this);
    return nullptr;
}

QT_END_NAMESPACE

#include "moc_qqmlcoverageservicefactory.cpp"
