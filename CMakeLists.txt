cmake_minimum_required(VERSION 3.14)
project(SimpleChat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Find dependencies
# ============================================================

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

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

# ============================================================
# Generate protobuf sources manually
# ============================================================
# We invoke protoc once to generate ALL .pb.cc/.pb.h files,
# then compile them into a static library. This guarantees
# every generated file exists before any compilation starts.

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/protos")
set(PROTO_OUT "${CMAKE_BINARY_DIR}/proto_gen")
file(MAKE_DIRECTORY ${PROTO_OUT})

# List of proto files (basenames only, paths handled below)
set(PROTO_BASENAMES
    common
    register
    message_general
    message_dm
    change_status
    list_users
    get_user_info
    quit
    server_response
    all_users
    for_dm
    broadcast_messages
    get_user_info_response
)

# Full paths to .proto source files
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

# Build lists of expected generated files
set(PROTO_SRCS "")
set(PROTO_HDRS "")
foreach(base ${PROTO_BASENAMES})
    list(APPEND PROTO_SRCS "${PROTO_OUT}/${base}.pb.cc")
    list(APPEND PROTO_HDRS "${PROTO_OUT}/${base}.pb.h")
endforeach()

# Single custom command that generates ALL proto files at once
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${PROTO_DIR}
        --cpp_out=${PROTO_OUT}
        ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating protobuf sources..."
    VERBATIM
)

# Custom target that depends on all generated files
add_custom_target(generate_protos DEPENDS ${PROTO_SRCS} ${PROTO_HDRS})

# Static library from generated protobuf code
add_library(proto_gen STATIC ${PROTO_SRCS} ${PROTO_HDRS})
set_target_properties(proto_gen PROPERTIES AUTOMOC OFF)
add_dependencies(proto_gen generate_protos)
target_include_directories(proto_gen PUBLIC ${PROTO_OUT})
target_include_directories(proto_gen PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(proto_gen ${Protobuf_LIBRARIES})

# ============================================================
# Include paths
# ============================================================

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${PROTO_OUT})

# ============================================================
# chat_server
# ============================================================

add_executable(chat_server
    src/server/main.cpp
    src/server/server.cpp
)
set_target_properties(chat_server PROPERTIES AUTOMOC OFF)
target_link_libraries(chat_server proto_gen Threads::Threads)

# ============================================================
# chat_client
# ============================================================

add_executable(chat_client
    src/client/main.cpp
    src/client/mainwindow.cpp
    src/client/network.cpp
)
set_target_properties(chat_client PROPERTIES AUTOMOC ON)
target_include_directories(chat_client PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
target_link_libraries(chat_client proto_gen ${QT_LIBS} Threads::Threads)
