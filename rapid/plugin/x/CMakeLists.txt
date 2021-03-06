# Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA


IF(NOT PROTOBUF_FOUND)
  MESSAGE(WARNING "Disabling mysqlx plugin.")
  RETURN()
ENDIF()

SET(MYSQLX_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
SET(MYSQLX_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
SET(MYSQLX_PLUGIN_NAME "mysqlx")

INCLUDE(source_files.cmake)

MYSQL_ADD_PLUGIN(${MYSQLX_PLUGIN_NAME}
  ${xplugin_all_SRC}
  MODULE_ONLY MODULE_OUTPUT_NAME ${MYSQLX_PLUGIN_NAME}
)

IF(WITHOUT_MYSQLX)
  RETURN()
ENDIF()

INCLUDE(${MYSQL_CMAKE_SCRIPT_DIR}/compile_flags.cmake)

IF(SSL_DEFINES)
  ADD_DEFINITIONS(${SSL_DEFINES})
ENDIF()

SET(XPLUGIN_LOG_PROTOBUF 1 CACHE STRING "Enable logging of protobuf messages")

INCLUDE(utils.cmake)
INCLUDE(variables.cmake)
INCLUDE(configure.cmake)

ADD_SUBDIRECTORY(protocol)
ADD_SUBDIRECTORY(client)
ADD_SUBDIRECTORY(tests)

OPTION(DISABLE_MYSQLX_PROTOBUF_LITE
  "Link mysqlx plugin with agains full version of protobuf/mysqlxmessages/mysqlxclient"
  OFF
)

IF(DISABLE_MYSQLX_PROTOBUF_LITE)
  SET(MYSQLX_PROTOBUF_LIB ${PROTOBUF_LIBRARY})
  SET(MYSQLX_PROTOCOL_LIB ${MYSQLX_PROTOCOL_FULL_LIB})
ELSE()
  SET(MYSQLX_PROTOBUF_LIB ${PROTOBUF_LITE_LIBRARY})
  SET(MYSQLX_PROTOCOL_LIB ${MYSQLX_PROTOCOL_LITE_LIB})
ENDIF()

INCLUDE_DIRECTORIES(
  ${MYSQLX_GENERATE_DIR}
  ${MYSQLX_PROTOCOL_INCLUDE_DIR}
  ${MYSQLX_PROJECT_DIR}
  ${MYSQLX_PROJECT_DIR}/src
  ${MYSQLX_PROJECT_DIR}/ngs/include
  ${MYSQLX_PROJECT_DIR}/client/
  ${CMAKE_SOURCE_DIR}/include
)

INCLUDE_DIRECTORIES(SYSTEM
  ${PROTOBUF_INCLUDE_DIRS}
  ${LIBEVENT_INCLUDE_DIR}
  ${SSL_INCLUDE_DIRS}
)

SET(MYSQLX_PROTBUF_AND_PROTOCOL_LIB
    ${MYSQLX_PROTOCOL_LIB}
)

IF(NOT "${WITH_PROTOBUF}" STREQUAL "system")
  SET(MYSQLX_PROTBUF_AND_PROTOCOL_LIB
    ${MYSQLX_PROTBUF_AND_PROTOCOL_LIB}
    ${MYSQLX_PROTOBUF_LIB}
  )
ENDIF()

ADD_COMPILE_FLAGS(${xplugin_all_SRC}
  COMPILE_FLAGS "${MYSQLX_PROTOCOL_FLAGS}"
)

# assertion failed in function assert_with_dumps() @ ssa.c:621 when using -xO3
IF(CMAKE_CXX_COMPILER_ID MATCHES "SunPro")
  ADD_COMPILE_FLAGS(${MYSQLX_PROJECT_DIR}/ngs/src/client.cc
    COMPILE_FLAGS "-xO2"
  )
ENDIF()

TARGET_LINK_LIBRARIES(${MYSQLX_PLUGIN_NAME}
  ${MYSQLX_PROTBUF_AND_PROTOCOL_LIB}
  ${LIBEVENT_LIBRARY}
  ${LIBEVENT_LIBRARIES}
)

ADD_DEPENDENCIES(${MYSQLX_PLUGIN_NAME} GenError xprotocol_tags)

IF(WITH_PROTOBUF STREQUAL "system")
  TARGET_LINK_LIBRARIES(${MYSQLX_PLUGIN_NAME} "${MYSQLX_PROTOBUF_LIB}")
ENDIF()

IF(WITH_LIBEVENT STREQUAL "bundled")
  ADD_DEPENDENCIES(${MYSQLX_PLUGIN_NAME} ${LIBEVENT_LIBRARY})
ENDIF()

IF(XPLUGIN_LOG_PROTOBUF)
  MESSAGE(STATUS "MYSQLX - Text log of protobuf messages enabled")
  ADD_DEFINITIONS(-DXPLUGIN_LOG_PROTOBUF)
ENDIF()

IF(XPLUGIN_LOG_DEBUG OR WITH_DEBUG)
  MESSAGE(STATUS "MYSQLX - Enabled debug traces")
  ADD_DEFINITIONS(-DXPLUGIN_LOG_DEBUG)
ENDIF()

IF(XPLUGIN_NO_LITE_PROTOBUF)
  MESSAGE(STATUS "MYSQLX - Using protobuf instead protobuf-lite")
ENDIF()
