# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

qt_get_tool_target_name(target_name qmlcoverage)
qt_internal_add_tool(${target_name}
    TARGET_DESCRIPTION "QML Code Coverage"
    TOOLS_TARGET Qml
    SOURCES
        main.cpp
        qmlcoveragereport.cpp qmlcoveragereport.h
    LIBRARIES
        Qt::Network
        Qt::PacketProtocolPrivate
        Qt::QmlDebugPrivate
)
qt_internal_return_unless_building_tools()
