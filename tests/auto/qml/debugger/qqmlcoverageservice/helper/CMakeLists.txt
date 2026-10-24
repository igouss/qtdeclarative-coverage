# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Built next to tst_qqmlcoverageservice, which runs it.
qt_internal_add_executable(qqmlcoverageservicehelper
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
    SOURCES
        main.cpp
    LIBRARIES
        Qt::Core
        Qt::Qml
        Qt::QmlPrivate
)

qt_internal_add_resource(qqmlcoverageservicehelper "resources"
    PREFIX "/"
    FILES
        resource.qml
)
