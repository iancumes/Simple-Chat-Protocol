// protocol.h – Common protocol definitions, TCP framing, and socket helpers.
// This file is used by both chat_server and chat_client.

#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// MSG_NOSIGNAL is Linux-specific; define fallback for other platforms
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// ============================================================
// Message Type IDs  (wire protocol – DO NOT CHANGE)
// ============================================================
namespace MsgType {
    // Client → Server
    constexpr uint8_t REGISTER        = 1;
    constexpr uint8_t MESSAGE_GENERAL = 2;
    constexpr uint8_t MESSAGE_DM      = 3;
    constexpr uint8_t CHANGE_STATUS   = 4;
    constexpr uint8_t LIST_USERS      = 5;
    constexpr uint8_t GET_USER_INFO   = 6;
    constexpr uint8_t QUIT            = 7;

    // Server → Client
    constexpr uint8_t SERVER_RESPONSE         = 10;
    constexpr uint8_t ALL_USERS               = 11;
    constexpr uint8_t FOR_DM                  = 12;
    constexpr uint8_t BROADCAST_DELIVERY      = 13;
    constexpr uint8_t GET_USER_INFO_RESPONSE  = 14;
}

// ============================================================
// ServerResponse status codes  (application-level)
// ============================================================
namespace StatusCode {
    constexpr int32_t OK_REGISTER         = 200;
    constexpr int32_t OK_STATUS_CHANGED   = 201;
    constexpr int32_t OK_DM_SENT          = 202;
    constexpr int32_t OK_QUIT             = 203;
    constexpr int32_t BAD_REQUEST         = 400;
    constexpr int32_t NOT_REGISTERED      = 401;
    constexpr int32_t USER_NOT_FOUND      = 404;
    constexpr int32_t USERNAME_TAKEN      = 409;
    constexpr int32_t IP_TAKEN            = 410;
    constexpr int32_t INTERNAL_ERROR      = 500;
    constexpr int32_t AUTO_INACTIVE       = 602;  // Server auto-set INVISIBLE
    constexpr int32_t STATUS_RESTORED     = 603;  // Restored after inactivity
}

// ============================================================
// TCP header size
// ============================================================
static constexpr size_t HEADER_SIZE = 5;  // 1 byte type + 4 bytes length

// ============================================================
// read_exact / write_exact – handle partial reads/writes
// ============================================================

// Returns true on success, false on EOF / error.
inline bool read_exact(int fd, void* buf, size_t len) {
    uint8_t* ptr = static_cast<uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::recv(fd, ptr, remaining, 0);
        if (n <= 0) return false;  // EOF or error
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

inline bool write_exact(int fd, const void* buf, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n <= 0) return false;
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

// ============================================================
// send_message – serialize proto + build header + send
// ============================================================
// T must have SerializeToString().
template <typename T>
bool send_message(int fd, uint8_t msg_type, const T& proto_msg) {
    std::string payload;
    if (!proto_msg.SerializeToString(&payload)) return false;

    uint8_t header[HEADER_SIZE];
    header[0] = msg_type;
    uint32_t len = static_cast<uint32_t>(payload.size());
    // big-endian
    header[1] = (len >> 24) & 0xFF;
    header[2] = (len >> 16) & 0xFF;
    header[3] = (len >> 8)  & 0xFF;
    header[4] =  len        & 0xFF;

    if (!write_exact(fd, header, HEADER_SIZE)) return false;
    if (len > 0 && !write_exact(fd, payload.data(), len)) return false;
    return true;
}

// ============================================================
// recv_message – read header, then payload
// ============================================================
// On success, sets msg_type and payload. Returns true.
// On EOF/error, returns false.
inline bool recv_message(int fd, uint8_t& msg_type, std::string& payload) {
    uint8_t header[HEADER_SIZE];
    if (!read_exact(fd, header, HEADER_SIZE)) return false;

    msg_type = header[0];
    uint32_t len = (static_cast<uint32_t>(header[1]) << 24)
                 | (static_cast<uint32_t>(header[2]) << 16)
                 | (static_cast<uint32_t>(header[3]) << 8)
                 |  static_cast<uint32_t>(header[4]);

    // Sanity check – reject absurdly large payloads (>16 MB)
    if (len > 16 * 1024 * 1024) return false;

    payload.resize(len);
    if (len > 0 && !read_exact(fd, &payload[0], len)) return false;
    return true;
}

// ============================================================
// Socket helpers
// ============================================================

// Get peer IP address as string (e.g. "192.168.1.5")
inline std::string get_peer_ip(int fd) {
    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen) == 0) {
        char buf[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
    }
    return "unknown";
}

// Get local IP of a connected socket (the interface used for this connection)
inline std::string get_local_ip(int fd) {
    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen) == 0) {
        char buf[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
    }
    return "0.0.0.0";
}

// ============================================================
// Status display helpers
// ============================================================
#include "common.pb.h"

inline const char* status_to_display(chat::StatusEnum s) {
    switch (s) {
        case chat::ACTIVE:         return "ACTIVO";
        case chat::DO_NOT_DISTURB: return "OCUPADO";
        case chat::INVISIBLE:      return "INACTIVO";
        default:                   return "DESCONOCIDO";
    }
}

inline const char* status_to_proto_name(chat::StatusEnum s) {
    switch (s) {
        case chat::ACTIVE:         return "ACTIVE";
        case chat::DO_NOT_DISTURB: return "DO_NOT_DISTURB";
        case chat::INVISIBLE:      return "INVISIBLE";
        default:                   return "UNKNOWN";
    }
}

#endif // CHAT_PROTOCOL_H
