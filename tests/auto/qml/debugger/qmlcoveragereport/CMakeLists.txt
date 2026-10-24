# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

if(NOT QT_BUILD_STANDALONE_TESTS AND NOT QT_BUILDING_QT)
    cmake_minimum_required(VERSION 3.16)
    project(tst_qmlcoveragereport LANGUAGES CXX)
    find_package(Qt6BuildInternals REQUIRED COMPONENTS STANDALONE_TEST)
endif()

set(tool_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../tools/qmlcoverage)

qt_internal_add_test(tst_qmlcoveragereport
    SOURCES
        tst_qmlcoveragereport.cpp
        ${tool_dir}/qmlcoveragereport.cpp ${tool_dir}/qmlcoveragereport.h
    INCLUDE_DIRECTORIES
        ${tool_dir}
    LIBRARIES
        Qt::PacketProtocolPrivate
        Qt::Test
)
