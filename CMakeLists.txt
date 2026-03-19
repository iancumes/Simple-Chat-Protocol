cmake_minimum_required(VERSION 3.14)
project(SimpleChat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# NOTE: Do NOT set CMAKE_AUTOMOC globally – it conflicts with protobuf generated headers.
# AUTOMOC is enabled per-target on chat_client only.

# ============================================================
# Find dependencies
# ============================================================

# Protobuf
find_package(Protobuf REQUIRED)

# Qt – try Qt6 first, fall back to Qt5
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Found Qt6 Widgets")
    set(QT_LIBS Qt6::Widgets)
else()
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    message(STATUS "Found Qt5 Widgets")
    set(QT_LIBS Qt5::Widgets)
endif()

# pthreads
find_package(Threads REQUIRED)

# ============================================================
# Generate protobuf sources
# ============================================================

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/protos")

set(PROTO_FILES
    ${PROTO_DIR}/common.proto
    ${PROTO_DIR}/cliente-side/register.proto
    ${PROTO_DIR}/cliente-side/message_general.proto
    ${PROTO_DIR}/cliente-side/message_dm.proto
    ${PROTO_DIR}/cliente-side/change_status.proto
    ${PROTO_DIR}/cliente-side/list_users.proto
    ${PROTO_DIR}/cliente-side/get_user_info.proto
    ${PROTO_DIR}/cliente-side/quit.proto
    ${PROTO_DIR}/server-side/server_response.proto
    ${PROTO_DIR}/server-side/all_users.proto
    ${PROTO_DIR}/server-side/for_dm.proto
    ${PROTO_DIR}/server-side/broadcast_messages.proto
    ${PROTO_DIR}/server-side/get_user_info_response.proto
)

# Generate .pb.cc and .pb.h into build directory
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Mark generated protobuf files so AUTOMOC skips them
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES
    SKIP_AUTOMOC ON
    SKIP_AUTOUIC ON
    SKIP_AUTORCC ON
)

# Make generated headers available
# protobuf_generate_cpp puts headers next to sources in CMAKE_CURRENT_BINARY_DIR
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# ============================================================
# Include paths
# ============================================================

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${Protobuf_INCLUDE_DIRS})

# ============================================================
# chat_server target
# ============================================================

add_executable(chat_server
    src/server/main.cpp
    src/server/server.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# Server does not use Qt – explicitly disable AUTOMOC
set_target_properties(chat_server PROPERTIES AUTOMOC OFF)

target_link_libraries(chat_server
    ${Protobuf_LIBRARIES}
    Threads::Threads
)

# ============================================================
# chat_client target
# ============================================================

add_executable(chat_client
    src/client/main.cpp
    src/client/mainwindow.cpp
    src/client/network.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# Enable AUTOMOC only for the client (it uses Qt signals/slots)
set_target_properties(chat_client PROPERTIES AUTOMOC ON)

# Client needs access to its own headers directory for MOC
target_include_directories(chat_client PRIVATE
    ${CMAKE_SOURCE_DIR}/src/client
)

target_link_libraries(chat_client
    ${Protobuf_LIBRARIES}
    ${QT_LIBS}
    Threads::Threads
)
