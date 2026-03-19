// mainwindow.h – Main chat client window (Qt Widgets).

#ifndef CHAT_CLIENT_MAINWINDOW_H
#define CHAT_CLIENT_MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QTabWidget>
#include <QSplitter>
#include <QAction>
#include <QToolBar>
#include <QCloseEvent>
#include <QMap>

#include "network.h"
#include "common.pb.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(const QString& username, int socket_fd, const std::string& local_ip,
               QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // UI actions
    void onSendClicked();
    void onRefreshUsers();
    void onChangeStatus(int index);
    void onUserDoubleClicked(QListWidgetItem* item);
    void onShowUserInfo();
    void onShowHelp();
    void onModeChanged(int index);

    // Network signals
    void onServerResponse(int statusCode, QString message, bool isSuccessful);
    void onAllUsers(QStringList usernames, QVector<int> statuses);
    void onForDm(QString senderUsername, QString message);
    void onBroadcast(QString usernameOrigin, QString message);
    void onUserInfo(QString ipAddress, QString username, int status);
    void onConnectionLost();

private:
    void setupUi();
    void setupStyleSheet();
    void appendSystemMessage(const QString& msg);
    void appendGeneralChat(const QString& user, const QString& msg);
    void appendDmChat(const QString& peerUser, const QString& msg, bool incoming);
    QTextEdit* getOrCreateDmTab(const QString& peerUser);
    QString statusToDisplay(int status);

    // Identity
    QString username_;
    std::string local_ip_;
    chat::StatusEnum current_status_ = chat::ACTIVE;

    // Network
    int socket_fd_;
    NetworkThread* net_thread_ = nullptr;
    NetworkSender* net_sender_ = nullptr;

    // UI widgets
    QToolBar*    toolbar_;
    QLabel*      statusLabel_;
    QLabel*      userLabel_;
    QComboBox*   statusCombo_;
    QListWidget* userList_;
    QTabWidget*  chatTabs_;
    QTextEdit*   generalChat_;
    QTextEdit*   systemLog_;
    QLineEdit*   messageInput_;
    QPushButton* sendButton_;
    QComboBox*   modeCombo_;       // "General" or "DM"
    QComboBox*   dmTargetCombo_;   // Target user for DM

    // Track DM tabs by peer username
    QMap<QString, QTextEdit*> dmTabs_;
};

#endif // CHAT_CLIENT_MAINWINDOW_H
