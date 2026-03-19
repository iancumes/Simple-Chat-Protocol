// network.h – Network thread for the chat client.
// Receives messages from the server in a background thread and emits Qt signals.

#ifndef CHAT_CLIENT_NETWORK_H
#define CHAT_CLIENT_NETWORK_H

#include <QObject>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QVector>
#include <mutex>
#include <atomic>

#include "chat/protocol.h"
#include "common.pb.h"
#include "server_response.pb.h"
#include "all_users.pb.h"
#include "for_dm.pb.h"
#include "broadcast_messages.pb.h"
#include "get_user_info_response.pb.h"

class NetworkThread : public QThread {
    Q_OBJECT

public:
    NetworkThread(int socket_fd, QObject* parent = nullptr);
    ~NetworkThread() override;

    void stopThread();

signals:
    // Server -> Client message signals
    void serverResponseReceived(int statusCode, QString message, bool isSuccessful);
    void allUsersReceived(QStringList usernames, QVector<int> statuses);
    void forDmReceived(QString senderUsername, QString message);
    void broadcastReceived(QString usernameOrigin, QString message);
    void userInfoReceived(QString ipAddress, QString username, int status);
    void connectionLost();

protected:
    void run() override;

private:
    int socket_fd_;
    std::atomic<bool> running_{true};
};

// Thread-safe send helper (locks a shared mutex before writing)
class NetworkSender {
public:
    NetworkSender(int socket_fd);

    bool sendRegister(const std::string& username, const std::string& ip);
    bool sendMessageGeneral(const std::string& message, chat::StatusEnum status,
                            const std::string& username, const std::string& ip);
    bool sendMessageDM(const std::string& message, chat::StatusEnum status,
                       const std::string& dest_user, const std::string& ip);
    bool sendChangeStatus(chat::StatusEnum status, const std::string& username,
                          const std::string& ip);
    bool sendListUsers(const std::string& username, const std::string& ip);
    bool sendGetUserInfo(const std::string& target_user, const std::string& username,
                         const std::string& ip);
    bool sendQuit(const std::string& ip);

private:
    int socket_fd_;
    std::mutex send_mutex_;
};

#endif // CHAT_CLIENT_NETWORK_H
