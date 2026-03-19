// network.cpp – Network thread and sender implementation.

#include "network.h"

#include "register.pb.h"
#include "message_general.pb.h"
#include "message_dm.pb.h"
#include "change_status.pb.h"
#include "list_users.pb.h"
#include "get_user_info.pb.h"
#include "quit.pb.h"

// ============================================================
// NetworkThread implementation
// ============================================================

NetworkThread::NetworkThread(int socket_fd, QObject* parent)
    : QThread(parent), socket_fd_(socket_fd) {}

NetworkThread::~NetworkThread() {
    stopThread();
    wait();
}

void NetworkThread::stopThread() {
    running_ = false;
    // Shutdown socket to unblock recv
    ::shutdown(socket_fd_, SHUT_RD);
}

void NetworkThread::run() {
    while (running_) {
        uint8_t msg_type;
        std::string payload;

        if (!recv_message(socket_fd_, msg_type, payload)) {
            if (running_) {
                emit connectionLost();
            }
            break;
        }

        switch (msg_type) {
            case MsgType::SERVER_RESPONSE: {
                chat::ServerResponse resp;
                if (resp.ParseFromString(payload)) {
                    emit serverResponseReceived(resp.status_code(),
                                                 QString::fromStdString(resp.message()),
                                                 resp.is_successful());
                }
                break;
            }
            case MsgType::ALL_USERS: {
                chat::AllUsers all;
                if (all.ParseFromString(payload)) {
                    QStringList names;
                    QVector<int> statuses;
                    for (int i = 0; i < all.usernames_size(); ++i) {
                        names.append(QString::fromStdString(all.usernames(i)));
                    }
                    for (int i = 0; i < all.status_size(); ++i) {
                        statuses.append(static_cast<int>(all.status(i)));
                    }
                    emit allUsersReceived(names, statuses);
                }
                break;
            }
            case MsgType::FOR_DM: {
                chat::ForDm dm;
                if (dm.ParseFromString(payload)) {
                    // username_des = sender's username (convention)
                    emit forDmReceived(QString::fromStdString(dm.username_des()),
                                       QString::fromStdString(dm.message()));
                }
                break;
            }
            case MsgType::BROADCAST_DELIVERY: {
                chat::BroadcastDelivery bd;
                if (bd.ParseFromString(payload)) {
                    emit broadcastReceived(QString::fromStdString(bd.username_origin()),
                                           QString::fromStdString(bd.message()));
                }
                break;
            }
            case MsgType::GET_USER_INFO_RESPONSE: {
                chat::GetUserInfoResponse info;
                if (info.ParseFromString(payload)) {
                    emit userInfoReceived(QString::fromStdString(info.ip_address()),
                                          QString::fromStdString(info.username()),
                                          static_cast<int>(info.status()));
                }
                break;
            }
            default:
                // Unknown message type from server – ignore
                break;
        }
    }
}

// ============================================================
// NetworkSender implementation
// ============================================================

NetworkSender::NetworkSender(int socket_fd)
    : socket_fd_(socket_fd) {}

bool NetworkSender::sendRegister(const std::string& username, const std::string& ip) {
    chat::Register reg;
    reg.set_username(username);
    reg.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::REGISTER, reg);
}

bool NetworkSender::sendMessageGeneral(const std::string& message, chat::StatusEnum status,
                                        const std::string& username, const std::string& ip) {
    chat::MessageGeneral msg;
    msg.set_message(message);
    msg.set_status(status);
    msg.set_username_origin(username);
    msg.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::MESSAGE_GENERAL, msg);
}

bool NetworkSender::sendMessageDM(const std::string& message, chat::StatusEnum status,
                                   const std::string& dest_user, const std::string& ip) {
    chat::MessageDM msg;
    msg.set_message(message);
    msg.set_status(status);
    msg.set_username_des(dest_user);
    msg.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::MESSAGE_DM, msg);
}

bool NetworkSender::sendChangeStatus(chat::StatusEnum status, const std::string& username,
                                      const std::string& ip) {
    chat::ChangeStatus cs;
    cs.set_status(status);
    cs.set_username(username);
    cs.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::CHANGE_STATUS, cs);
}

bool NetworkSender::sendListUsers(const std::string& username, const std::string& ip) {
    chat::ListUsers lu;
    lu.set_username(username);
    lu.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::LIST_USERS, lu);
}

bool NetworkSender::sendGetUserInfo(const std::string& target_user, const std::string& username,
                                     const std::string& ip) {
    chat::GetUserInfo gui;
    gui.set_username_des(target_user);
    gui.set_username(username);
    gui.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::GET_USER_INFO, gui);
}

bool NetworkSender::sendQuit(const std::string& ip) {
    chat::Quit q;
    q.set_quit(true);
    q.set_ip(ip);
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_message(socket_fd_, MsgType::QUIT, q);
}
