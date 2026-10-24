# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

qt_internal_add_executable(qmlcoveragetoolhelper
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
    SOURCES
        main.cpp
    LIBRARIES
        Qt::Core
        Qt::Qml
)

qt_internal_add_resource(qmlcoveragetoolhelper "resources"
    PREFIX "/"
    FILES
        resource.qml
)
