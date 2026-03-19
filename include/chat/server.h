// server.h – Chat server class declaration.

#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

#include "chat/protocol.h"
#include "common.pb.h"

struct SessionInfo {
    std::string username;
    std::string ip;
    int         socket_fd = -1;
    chat::StatusEnum status = chat::ACTIVE;
    chat::StatusEnum manual_status = chat::ACTIVE;  // Last manually-set status
    std::chrono::steady_clock::time_point last_activity;
    std::atomic<bool> alive{true};
    std::mutex send_mutex;  // Serialize writes to this socket

    SessionInfo() = default;
    SessionInfo(const std::string& user, const std::string& addr, int fd)
        : username(user), ip(addr), socket_fd(fd),
          status(chat::ACTIVE), manual_status(chat::ACTIVE),
          last_activity(std::chrono::steady_clock::now()) {}

    // Non-copyable due to mutex/atomic; use via shared_ptr
    SessionInfo(const SessionInfo&) = delete;
    SessionInfo& operator=(const SessionInfo&) = delete;
};

class ChatServer {
public:
    ChatServer(uint16_t port, int idle_timeout_sec = 180);
    ~ChatServer();

    void run();    // Blocking – runs accept loop
    void stop();

private:
    uint16_t port_;
    int idle_timeout_sec_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};

    // Session registry – protected by sessions_mutex_
    std::mutex sessions_mutex_;
    std::unordered_map<int, std::shared_ptr<SessionInfo>> sessions_;  // fd -> session
    std::unordered_map<std::string, int> username_to_fd_;             // username -> fd
    std::unordered_map<std::string, int> ip_to_fd_;                   // ip -> fd

    // Threads
    std::vector<std::thread> client_threads_;
    std::thread watchdog_thread_;

    // Client handling
    void handle_client(int client_fd);
    void remove_session(int client_fd);

    // Update last activity timestamp
    void touch_activity(std::shared_ptr<SessionInfo>& session);
    // Restore from auto-inactivity if needed
    void maybe_restore_activity(std::shared_ptr<SessionInfo>& session);

    // Message handlers
    void handle_register(int fd, const std::string& payload);
    void handle_message_general(int fd, const std::string& payload);
    void handle_message_dm(int fd, const std::string& payload);
    void handle_change_status(int fd, const std::string& payload);
    void handle_list_users(int fd, const std::string& payload);
    void handle_get_user_info(int fd, const std::string& payload);
    void handle_quit(int fd, const std::string& payload);

    // Helpers
    void send_server_response(int fd, int32_t code, const std::string& msg, bool success);
    void send_to_client(int fd, uint8_t msg_type, const google::protobuf::MessageLite& proto);
    std::shared_ptr<SessionInfo> get_session(int fd);

    // Watchdog
    void watchdog_loop();
};

#endif // CHAT_SERVER_H
