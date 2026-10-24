# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

qt_internal_add_plugin(QQmlCoverageServiceFactoryPlugin
    OUTPUT_NAME qmldbg_coverage
    CLASS_NAME QQmlCoverageServiceFactory
    PLUGIN_TYPE qmltooling
    SOURCES
        qqmlcoverageservice.cpp qqmlcoverageservice.h
        qqmlcoverageservicefactory.cpp qqmlcoverageservicefactory.h
    LIBRARIES
        Qt::Core
        Qt::PacketProtocolPrivate
        Qt::QmlPrivate
)
