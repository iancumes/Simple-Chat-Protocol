// server.cpp – Full chat server implementation.

#include "chat/server.h"
#include "chat/protocol.h"

// Protobuf generated headers
#include "common.pb.h"
#include "register.pb.h"
#include "message_general.pb.h"
#include "message_dm.pb.h"
#include "change_status.pb.h"
#include "list_users.pb.h"
#include "get_user_info.pb.h"
#include "quit.pb.h"
#include "server_response.pb.h"
#include "all_users.pb.h"
#include "for_dm.pb.h"
#include "broadcast_messages.pb.h"
#include "get_user_info_response.pb.h"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <csignal>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ============================================================
// Internal helper: send a protobuf message to fd, locking session->send_mutex.
// Caller must NOT hold sessions_mutex_ if using this via send_to_client().
// If caller already holds sessions_mutex_, use send_locked() directly.
// ============================================================

// send with send_mutex lock on a known session. No sessions_mutex_ dependency.
static void send_locked(std::shared_ptr<SessionInfo>& session, uint8_t msg_type,
                         const google::protobuf::MessageLite& proto) {
    std::lock_guard<std::mutex> slock(session->send_mutex);
    send_message(session->socket_fd, msg_type, proto);
}

// Build and send a ServerResponse directly with a session reference.
// Does NOT acquire sessions_mutex_.
static void send_response_direct(std::shared_ptr<SessionInfo>& session,
                                  int32_t code, const std::string& msg, bool success) {
    chat::ServerResponse resp;
    resp.set_status_code(code);
    resp.set_message(msg);
    resp.set_is_successful(success);
    send_locked(session, MsgType::SERVER_RESPONSE, resp);
}

// Send ServerResponse to a bare fd (no session yet, e.g. pre-registration).
// Only safe when we know exactly one thread writes to this fd.
static void send_response_bare(int fd, int32_t code, const std::string& msg, bool success) {
    chat::ServerResponse resp;
    resp.set_status_code(code);
    resp.set_message(msg);
    resp.set_is_successful(success);
    send_message(fd, MsgType::SERVER_RESPONSE, resp);
}

// ============================================================
// Constructor / Destructor
// ============================================================

ChatServer::ChatServer(uint16_t port, int idle_timeout_sec)
    : port_(port), idle_timeout_sec_(idle_timeout_sec) {}

ChatServer::~ChatServer() {
    stop();
}

// ============================================================
// run() – setup listening socket and accept loop
// ============================================================

void ChatServer::run() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[SERVER] Error creating socket: " << strerror(errno) << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[SERVER] Error binding to port " << port_ << ": " << strerror(errno) << std::endl;
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, 64) < 0) {
        std::cerr << "[SERVER] Error listening: " << strerror(errno) << std::endl;
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    running_ = true;
    std::cout << "[SERVER] Listening on port " << port_
              << " (idle timeout: " << idle_timeout_sec_ << "s)" << std::endl;

    // Start watchdog thread
    watchdog_thread_ = std::thread(&ChatServer::watchdog_loop, this);

    // Accept loop
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
                                 &client_len);
        if (client_fd < 0) {
            if (running_) {
                std::cerr << "[SERVER] Accept error: " << strerror(errno) << std::endl;
            }
            continue;
        }

        std::string peer_ip = get_peer_ip(client_fd);
        std::cout << "[SERVER] New connection from " << peer_ip
                  << " (fd=" << client_fd << ")" << std::endl;

        // Spawn client handler thread
        client_threads_.emplace_back(&ChatServer::handle_client, this, client_fd);
    }
}

void ChatServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }

    // Close all client sockets
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [fd, session] : sessions_) {
            session->alive = false;
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
    }

    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
}

// ============================================================
// handle_client – per-client read loop
// ============================================================

void ChatServer::handle_client(int client_fd) {
    while (running_) {
        uint8_t msg_type;
        std::string payload;

        if (!recv_message(client_fd, msg_type, payload)) {
            // Connection closed or error
            std::cout << "[SERVER] Client fd=" << client_fd << " disconnected." << std::endl;
            break;
        }

        // Dispatch based on message type
        switch (msg_type) {
            case MsgType::REGISTER:
                handle_register(client_fd, payload);
                break;
            case MsgType::MESSAGE_GENERAL:
                handle_message_general(client_fd, payload);
                break;
            case MsgType::MESSAGE_DM:
                handle_message_dm(client_fd, payload);
                break;
            case MsgType::CHANGE_STATUS:
                handle_change_status(client_fd, payload);
                break;
            case MsgType::LIST_USERS:
                handle_list_users(client_fd, payload);
                break;
            case MsgType::GET_USER_INFO:
                handle_get_user_info(client_fd, payload);
                break;
            case MsgType::QUIT:
                handle_quit(client_fd, payload);
                goto cleanup;  // Exit read loop after quit
            default:
                std::cerr << "[SERVER] Unknown message type " << (int)msg_type
                          << " from fd=" << client_fd << std::endl;
                // Can't use send_server_response here if no session — use bare send
                send_response_bare(client_fd, StatusCode::BAD_REQUEST,
                                   "Unknown message type", false);
                break;
        }
    }

cleanup:
    remove_session(client_fd);
    ::close(client_fd);
}

// ============================================================
// Session management
// ============================================================

void ChatServer::remove_session(int client_fd) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        auto& session = it->second;
        session->alive = false;
        std::cout << "[SERVER] Removing session: " << session->username
                  << " (" << session->ip << ")" << std::endl;
        username_to_fd_.erase(session->username);
        ip_to_fd_.erase(session->ip);
        sessions_.erase(it);
    }
}

std::shared_ptr<SessionInfo> ChatServer::get_session(int fd) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(fd);
    if (it != sessions_.end()) return it->second;
    return nullptr;
}

void ChatServer::touch_activity(std::shared_ptr<SessionInfo>& session) {
    session->last_activity = std::chrono::steady_clock::now();
}

void ChatServer::maybe_restore_activity(std::shared_ptr<SessionInfo>& session) {
    // If the server auto-set the user to INVISIBLE due to inactivity,
    // restore to ACTIVE on next real user action.
    // Note: session fields are only written by the owning client thread (here)
    // or the watchdog, which holds sessions_mutex_. Since this is called from
    // the client's own handler thread, accessing session->status is safe here
    // because the shared_ptr keeps the object alive and status transitions
    // for this specific session happen sequentially in practice.
    if (session->status == chat::INVISIBLE && session->manual_status != chat::INVISIBLE) {
        session->status = chat::ACTIVE;
        session->manual_status = chat::ACTIVE;
        std::cout << "[SERVER] Restored " << session->username << " to ACTIVE after activity." << std::endl;
        send_response_direct(session, StatusCode::STATUS_RESTORED,
                             "Status restored to ACTIVO after inactivity period", true);
    }
    touch_activity(session);
}

// ============================================================
// send_server_response / send_to_client
// These do NOT hold sessions_mutex_ – they acquire it via get_session()
// ============================================================

void ChatServer::send_server_response(int fd, int32_t code, const std::string& msg, bool success) {
    auto session = get_session(fd);
    if (session) {
        send_response_direct(session, code, msg, success);
    } else {
        send_response_bare(fd, code, msg, success);
    }
}

void ChatServer::send_to_client(int fd, uint8_t msg_type, const google::protobuf::MessageLite& proto) {
    auto session = get_session(fd);
    if (session) {
        send_locked(session, msg_type, proto);
    } else {
        send_message(fd, msg_type, proto);
    }
}

// ============================================================
// Message handlers
// ============================================================

void ChatServer::handle_register(int fd, const std::string& payload) {
    chat::Register reg;
    if (!reg.ParseFromString(payload)) {
        send_response_bare(fd, StatusCode::BAD_REQUEST, "Malformed Register message", false);
        return;
    }

    std::string username = reg.username();
    std::string peer_ip  = get_peer_ip(fd);  // Use real peer IP as truth

    if (username.empty()) {
        send_response_bare(fd, StatusCode::BAD_REQUEST, "Username cannot be empty", false);
        return;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Check if this fd already has a session (double registration)
    if (sessions_.count(fd)) {
        // Already registered – send directly since we hold sessions_mutex_
        auto& s = sessions_[fd];
        send_response_direct(s, StatusCode::BAD_REQUEST, "Already registered", false);
        return;
    }

    // Check username uniqueness
    if (username_to_fd_.count(username)) {
        // No session for this fd yet, use bare send
        send_response_bare(fd, StatusCode::USERNAME_TAKEN,
                           "Username '" + username + "' is already taken", false);
        return;
    }

    // Check IP uniqueness
    if (ip_to_fd_.count(peer_ip)) {
        send_response_bare(fd, StatusCode::IP_TAKEN,
                           "IP " + peer_ip + " is already registered", false);
        return;
    }

    // Create session
    auto session = std::make_shared<SessionInfo>(username, peer_ip, fd);
    sessions_[fd] = session;
    username_to_fd_[username] = fd;
    ip_to_fd_[peer_ip] = fd;

    std::cout << "[SERVER] Registered: " << username << " (" << peer_ip << ")" << std::endl;

    // Send success – use send_response_direct which only locks send_mutex (no deadlock)
    send_response_direct(session, StatusCode::OK_REGISTER,
                         "Registration successful. Welcome, " + username + "!", true);
}

void ChatServer::handle_message_general(int fd, const std::string& payload) {
    auto session = get_session(fd);
    if (!session) {
        send_response_bare(fd, StatusCode::NOT_REGISTERED, "Not registered", false);
        return;
    }

    chat::MessageGeneral msg;
    if (!msg.ParseFromString(payload)) {
        send_response_direct(session, StatusCode::BAD_REQUEST, "Malformed MessageGeneral", false);
        return;
    }

    maybe_restore_activity(session);

    // Use session username, not client-supplied
    std::string sender = session->username;
    std::string text   = msg.message();

    std::cout << "[SERVER] Broadcast from " << sender << ": " << text << std::endl;

    // Build BroadcastDelivery
    chat::BroadcastDelivery broadcast;
    broadcast.set_message(text);
    broadcast.set_username_origin(sender);

    // Collect sessions under lock, then send outside lock to avoid holding
    // sessions_mutex_ while doing I/O
    std::vector<std::shared_ptr<SessionInfo>> targets;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [cfd, csession] : sessions_) {
            if (csession->alive) {
                targets.push_back(csession);
            }
        }
    }

    // Send to all – each send_locked only holds the per-session send_mutex
    for (auto& t : targets) {
        send_locked(t, MsgType::BROADCAST_DELIVERY, broadcast);
    }
}

void ChatServer::handle_message_dm(int fd, const std::string& payload) {
    auto session = get_session(fd);
    if (!session) {
        send_response_bare(fd, StatusCode::NOT_REGISTERED, "Not registered", false);
        return;
    }

    chat::MessageDM msg;
    if (!msg.ParseFromString(payload)) {
        send_response_direct(session, StatusCode::BAD_REQUEST, "Malformed MessageDM", false);
        return;
    }

    maybe_restore_activity(session);

    std::string sender      = session->username;
    std::string dest_user   = msg.username_des();
    std::string text        = msg.message();

    std::cout << "[SERVER] DM from " << sender << " to " << dest_user << ": " << text << std::endl;

    // Find destination under lock
    std::shared_ptr<SessionInfo> dest_session;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto dit = username_to_fd_.find(dest_user);
        if (dit != username_to_fd_.end()) {
            auto sit = sessions_.find(dit->second);
            if (sit != sessions_.end()) {
                dest_session = sit->second;
            }
        }
    }
    // sessions_mutex_ is released here

    if (!dest_session) {
        send_response_direct(session, StatusCode::USER_NOT_FOUND,
                             "User '" + dest_user + "' not found", false);
        return;
    }

    // Build ForDm – Convention: username_des = sender's username (so recipient knows who sent it)
    chat::ForDm fordm;
    fordm.set_username_des(sender);
    fordm.set_message(text);

    if (dest_session->alive) {
        send_locked(dest_session, MsgType::FOR_DM, fordm);
    }

    // Send success to sender
    send_response_direct(session, StatusCode::OK_DM_SENT, "DM sent to " + dest_user, true);
}

void ChatServer::handle_change_status(int fd, const std::string& payload) {
    auto session = get_session(fd);
    if (!session) {
        send_response_bare(fd, StatusCode::NOT_REGISTERED, "Not registered", false);
        return;
    }

    chat::ChangeStatus cs;
    if (!cs.ParseFromString(payload)) {
        send_response_direct(session, StatusCode::BAD_REQUEST, "Malformed ChangeStatus", false);
        return;
    }

    chat::StatusEnum new_status = cs.status();

    // Validate status value
    if (new_status != chat::ACTIVE && new_status != chat::DO_NOT_DISTURB && new_status != chat::INVISIBLE) {
        send_response_direct(session, StatusCode::BAD_REQUEST, "Invalid status value", false);
        return;
    }

    session->status = new_status;
    session->manual_status = new_status;
    touch_activity(session);

    std::cout << "[SERVER] " << session->username << " changed status to "
              << status_to_display(new_status) << std::endl;

    send_response_direct(session, StatusCode::OK_STATUS_CHANGED,
                         std::string("Status changed to ") + status_to_display(new_status), true);
}

void ChatServer::handle_list_users(int fd, const std::string& payload) {
    auto session = get_session(fd);
    if (!session) {
        send_response_bare(fd, StatusCode::NOT_REGISTERED, "Not registered", false);
        return;
    }

    chat::ListUsers lu;
    if (!lu.ParseFromString(payload)) {
        send_response_direct(session, StatusCode::BAD_REQUEST, "Malformed ListUsers", false);
        return;
    }

    // Do NOT count list_users as activity that restores from inactivity automatically.
    // Only touch the timestamp to prevent timeout while actively using the app.
    touch_activity(session);

    // Build AllUsers response – sorted alphabetically
    chat::AllUsers all;
    std::vector<std::pair<std::string, chat::StatusEnum>> users;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [cfd, csession] : sessions_) {
            users.emplace_back(csession->username, csession->status);
        }
    }

    std::sort(users.begin(), users.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (auto& [name, st] : users) {
        all.add_usernames(name);
        all.add_status(st);
    }

    send_locked(session, MsgType::ALL_USERS, all);
}

void ChatServer::handle_get_user_info(int fd, const std::string& payload) {
    auto session = get_session(fd);
    if (!session) {
        send_response_bare(fd, StatusCode::NOT_REGISTERED, "Not registered", false);
        return;
    }

    chat::GetUserInfo gui;
    if (!gui.ParseFromString(payload)) {
        send_response_direct(session, StatusCode::BAD_REQUEST, "Malformed GetUserInfo", false);
        return;
    }

    touch_activity(session);

    std::string target_user = gui.username_des();

    // Find target under lock
    std::shared_ptr<SessionInfo> target_session;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = username_to_fd_.find(target_user);
        if (it != username_to_fd_.end()) {
            auto sit = sessions_.find(it->second);
            if (sit != sessions_.end()) {
                target_session = sit->second;
            }
        }
    }

    if (!target_session) {
        send_response_direct(session, StatusCode::USER_NOT_FOUND,
                             "User '" + target_user + "' not found", false);
        return;
    }

    chat::GetUserInfoResponse info_resp;
    info_resp.set_ip_address(target_session->ip);
    info_resp.set_username(target_session->username);
    info_resp.set_status(target_session->status);

    send_locked(session, MsgType::GET_USER_INFO_RESPONSE, info_resp);
}

void ChatServer::handle_quit(int fd, const std::string& payload) {
    auto session = get_session(fd);

    chat::Quit q;
    if (!q.ParseFromString(payload)) {
        std::cerr << "[SERVER] Malformed Quit from fd=" << fd << std::endl;
        return;
    }

    if (session) {
        std::cout << "[SERVER] " << session->username << " sent Quit." << std::endl;
        send_response_direct(session, StatusCode::OK_QUIT, "Goodbye!", true);
    }
}

// ============================================================
// Watchdog – inactivity detection
// ============================================================

void ChatServer::watchdog_loop() {
    while (running_) {
        // Check every 10 seconds
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!running_) break;

        auto now = std::chrono::steady_clock::now();

        // Collect sessions that need timeout notification
        std::vector<std::shared_ptr<SessionInfo>> to_notify;

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto& [fd, session] : sessions_) {
                if (!session->alive) continue;
                if (session->status == chat::INVISIBLE) continue;

                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - session->last_activity).count();

                if (elapsed >= idle_timeout_sec_) {
                    std::cout << "[SERVER] " << session->username
                              << " marked INACTIVO after " << elapsed << "s idle." << std::endl;
                    session->status = chat::INVISIBLE;
                    // Do NOT update manual_status – keep the last manual choice
                    to_notify.push_back(session);
                }
            }
        }
        // sessions_mutex_ released – now safe to send

        for (auto& session : to_notify) {
            send_response_direct(session, StatusCode::AUTO_INACTIVE,
                                 "You have been marked INACTIVO due to inactivity (" +
                                 std::to_string(idle_timeout_sec_) + "s)", true);
        }
    }
}
