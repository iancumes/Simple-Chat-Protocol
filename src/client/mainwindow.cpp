// mainwindow.cpp – Main chat client window implementation.

#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMenuBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QApplication>
#include <QFont>
#include <QGroupBox>
#include <QStatusBar>
#include <QScrollBar>
#include <QTimer>

// ============================================================
// Constructor
// ============================================================

MainWindow::MainWindow(const QString& username, int socket_fd,
                       const std::string& local_ip, QWidget* parent)
    : QMainWindow(parent),
      username_(username),
      local_ip_(local_ip),
      socket_fd_(socket_fd)
{
    setupUi();
    setupStyleSheet();

    // Create network sender
    net_sender_ = new NetworkSender(socket_fd_);

    // Create and start network receiver thread
    net_thread_ = new NetworkThread(socket_fd_, this);
    connect(net_thread_, &NetworkThread::serverResponseReceived,
            this, &MainWindow::onServerResponse, Qt::QueuedConnection);
    connect(net_thread_, &NetworkThread::allUsersReceived,
            this, &MainWindow::onAllUsers, Qt::QueuedConnection);
    connect(net_thread_, &NetworkThread::forDmReceived,
            this, &MainWindow::onForDm, Qt::QueuedConnection);
    connect(net_thread_, &NetworkThread::broadcastReceived,
            this, &MainWindow::onBroadcast, Qt::QueuedConnection);
    connect(net_thread_, &NetworkThread::userInfoReceived,
            this, &MainWindow::onUserInfo, Qt::QueuedConnection);
    connect(net_thread_, &NetworkThread::connectionLost,
            this, &MainWindow::onConnectionLost, Qt::QueuedConnection);
    net_thread_->start();

    // Request initial user list
    QTimer::singleShot(500, this, &MainWindow::onRefreshUsers);
}

MainWindow::~MainWindow() {
    delete net_sender_;
}

// ============================================================
// UI Setup
// ============================================================

void MainWindow::setupUi() {
    setWindowTitle("Chat – " + username_);
    resize(900, 620);

    // --- Toolbar ---
    toolbar_ = addToolBar("Main");
    toolbar_->setMovable(false);

    userLabel_ = new QLabel("  👤 " + username_ + "  ");
    userLabel_->setStyleSheet("font-weight: bold; font-size: 13px;");
    toolbar_->addWidget(userLabel_);

    toolbar_->addSeparator();

    QLabel* statusLbl = new QLabel("  Estado: ");
    toolbar_->addWidget(statusLbl);

    statusCombo_ = new QComboBox();
    statusCombo_->addItem("🟢 ACTIVO",     static_cast<int>(chat::ACTIVE));
    statusCombo_->addItem("🔴 OCUPADO",    static_cast<int>(chat::DO_NOT_DISTURB));
    statusCombo_->addItem("⚫ INACTIVO",   static_cast<int>(chat::INVISIBLE));
    connect(statusCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChangeStatus);
    toolbar_->addWidget(statusCombo_);

    toolbar_->addSeparator();

    QAction* refreshAct = toolbar_->addAction("🔄 Usuarios");
    connect(refreshAct, &QAction::triggered, this, &MainWindow::onRefreshUsers);

    QAction* infoAct = toolbar_->addAction("ℹ️ Info Usuario");
    connect(infoAct, &QAction::triggered, this, &MainWindow::onShowUserInfo);

    QAction* helpAct = toolbar_->addAction("❓ Ayuda");
    connect(helpAct, &QAction::triggered, this, &MainWindow::onShowHelp);

    toolbar_->addSeparator();

    statusLabel_ = new QLabel("  🟢 Conectado  ");
    statusLabel_->setStyleSheet("color: #2ecc71; font-weight: bold;");
    toolbar_->addWidget(statusLabel_);

    // --- Central Widget ---
    QWidget* central = new QWidget();
    setCentralWidget(central);

    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // -- Left panel: User list --
    QVBoxLayout* leftLayout = new QVBoxLayout();
    QLabel* usersTitle = new QLabel("Usuarios Conectados");
    usersTitle->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px;");
    usersTitle->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(usersTitle);

    userList_ = new QListWidget();
    userList_->setMinimumWidth(180);
    userList_->setMaximumWidth(220);
    connect(userList_, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onUserDoubleClicked);
    leftLayout->addWidget(userList_);

    mainLayout->addLayout(leftLayout);

    // -- Right panel: Chat + System log + Input --
    QVBoxLayout* rightLayout = new QVBoxLayout();

    // Chat tabs
    chatTabs_ = new QTabWidget();
    chatTabs_->setTabsClosable(true);
    connect(chatTabs_, &QTabWidget::tabCloseRequested, [this](int index) {
        // Don't allow closing General or System tabs
        if (index <= 1) return;
        QString tabName = chatTabs_->tabText(index);
        dmTabs_.remove(tabName);
        chatTabs_->removeTab(index);
    });

    // General chat tab
    generalChat_ = new QTextEdit();
    generalChat_->setReadOnly(true);
    chatTabs_->addTab(generalChat_, "💬 General");

    // System log tab
    systemLog_ = new QTextEdit();
    systemLog_->setReadOnly(true);
    chatTabs_->addTab(systemLog_, "🖥️ Sistema");

    rightLayout->addWidget(chatTabs_, 1);

    // -- Input area --
    QHBoxLayout* inputLayout = new QHBoxLayout();

    modeCombo_ = new QComboBox();
    modeCombo_->addItem("General");
    modeCombo_->addItem("DM");
    modeCombo_->setFixedWidth(90);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
    inputLayout->addWidget(modeCombo_);

    dmTargetCombo_ = new QComboBox();
    dmTargetCombo_->setFixedWidth(140);
    dmTargetCombo_->setPlaceholderText("Destinatario");
    dmTargetCombo_->setVisible(false);
    inputLayout->addWidget(dmTargetCombo_);

    messageInput_ = new QLineEdit();
    messageInput_->setPlaceholderText("Escribe un mensaje...");
    connect(messageInput_, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    inputLayout->addWidget(messageInput_, 1);

    sendButton_ = new QPushButton("Enviar");
    sendButton_->setFixedWidth(80);
    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    inputLayout->addWidget(sendButton_);

    rightLayout->addLayout(inputLayout);

    mainLayout->addLayout(rightLayout, 1);
}

void MainWindow::setupStyleSheet() {
    setStyleSheet(R"(
        QMainWindow {
            background-color: #1e1e2e;
            color: #cdd6f4;
        }
        QToolBar {
            background-color: #181825;
            border-bottom: 1px solid #313244;
            padding: 4px;
            spacing: 6px;
        }
        QToolBar QLabel {
            color: #cdd6f4;
        }
        QToolBar QAction {
            color: #cdd6f4;
        }
        QPushButton {
            background-color: #89b4fa;
            color: #1e1e2e;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #74c7ec;
        }
        QPushButton:pressed {
            background-color: #89dceb;
        }
        QLineEdit {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 13px;
        }
        QLineEdit:focus {
            border-color: #89b4fa;
        }
        QComboBox {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 4px 8px;
            font-size: 12px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #313244;
            color: #cdd6f4;
            selection-background-color: #45475a;
        }
        QTextEdit {
            background-color: #181825;
            color: #cdd6f4;
            border: 1px solid #313244;
            border-radius: 6px;
            padding: 6px;
            font-family: 'Monospace', 'Courier New', monospace;
            font-size: 12px;
        }
        QListWidget {
            background-color: #181825;
            color: #cdd6f4;
            border: 1px solid #313244;
            border-radius: 6px;
            padding: 4px;
            font-size: 12px;
        }
        QListWidget::item {
            padding: 6px 8px;
            border-radius: 4px;
        }
        QListWidget::item:hover {
            background-color: #313244;
        }
        QListWidget::item:selected {
            background-color: #45475a;
            color: #cdd6f4;
        }
        QTabWidget::pane {
            border: 1px solid #313244;
            border-radius: 6px;
            background-color: #181825;
        }
        QTabBar::tab {
            background-color: #313244;
            color: #a6adc8;
            padding: 6px 14px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background-color: #181825;
            color: #cdd6f4;
            font-weight: bold;
        }
        QTabBar::tab:hover {
            background-color: #45475a;
        }
        QLabel {
            color: #cdd6f4;
        }
        QStatusBar {
            background-color: #181825;
            color: #a6adc8;
        }
    )");
}

// ============================================================
// Helper methods
// ============================================================

QString MainWindow::statusToDisplay(int status) {
    switch (static_cast<chat::StatusEnum>(status)) {
        case chat::ACTIVE:         return "🟢 ACTIVO";
        case chat::DO_NOT_DISTURB: return "🔴 OCUPADO";
        case chat::INVISIBLE:      return "⚫ INACTIVO";
        default:                   return "❓ DESCONOCIDO";
    }
}

void MainWindow::appendSystemMessage(const QString& msg) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    systemLog_->append("<span style='color:#a6adc8;'>[" + timestamp + "]</span> " + msg);

    // Auto-scroll
    QScrollBar* sb = systemLog_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::appendGeneralChat(const QString& user, const QString& msg) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString color = (user == username_) ? "#89b4fa" : "#f38ba8";
    generalChat_->append(
        "<span style='color:#a6adc8;'>[" + timestamp + "]</span> "
        "<b style='color:" + color + ";'>" + user.toHtmlEscaped() + ":</b> " +
        msg.toHtmlEscaped());

    QScrollBar* sb = generalChat_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::appendDmChat(const QString& peerUser, const QString& msg, bool incoming) {
    QTextEdit* tab = getOrCreateDmTab(peerUser);
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString label = incoming ? peerUser : username_;
    QString color = incoming ? "#f38ba8" : "#89b4fa";

    tab->append(
        "<span style='color:#a6adc8;'>[" + timestamp + "]</span> "
        "<b style='color:" + color + ";'>" + label.toHtmlEscaped() + ":</b> " +
        msg.toHtmlEscaped());

    QScrollBar* sb = tab->verticalScrollBar();
    sb->setValue(sb->maximum());

    // If incoming, switch to that tab
    if (incoming) {
        int idx = chatTabs_->indexOf(tab);
        if (idx >= 0) chatTabs_->setCurrentIndex(idx);
    }
}

QTextEdit* MainWindow::getOrCreateDmTab(const QString& peerUser) {
    if (dmTabs_.contains(peerUser)) {
        return dmTabs_[peerUser];
    }
    QTextEdit* tab = new QTextEdit();
    tab->setReadOnly(true);
    int idx = chatTabs_->addTab(tab, "📩 " + peerUser);
    dmTabs_[peerUser] = tab;
    return tab;
}

// ============================================================
// UI Action Slots
// ============================================================

void MainWindow::onSendClicked() {
    QString text = messageInput_->text().trimmed();
    if (text.isEmpty()) return;

    messageInput_->clear();

    if (modeCombo_->currentIndex() == 0) {
        // General mode
        net_sender_->sendMessageGeneral(text.toStdString(), current_status_,
                                         username_.toStdString(), local_ip_);
        // Don't show local echo – we'll see it via BroadcastDelivery from server
    } else {
        // DM mode
        QString target = dmTargetCombo_->currentText();
        if (target.isEmpty()) {
            appendSystemMessage("<span style='color:#fab387;'>⚠ Selecciona un destinatario para DM.</span>");
            return;
        }
        net_sender_->sendMessageDM(text.toStdString(), current_status_,
                                    target.toStdString(), local_ip_);
        // Show local echo for DM
        appendDmChat(target, text, false);
    }
}

void MainWindow::onRefreshUsers() {
    net_sender_->sendListUsers(username_.toStdString(), local_ip_);
}

void MainWindow::onChangeStatus(int index) {
    int statusVal = statusCombo_->itemData(index).toInt();
    chat::StatusEnum newStatus = static_cast<chat::StatusEnum>(statusVal);

    if (newStatus == current_status_) return;

    net_sender_->sendChangeStatus(newStatus, username_.toStdString(), local_ip_);
    // Status will be updated when we receive the ServerResponse confirmation
}

void MainWindow::onUserDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    // Extract username from the list item (format: "username  status")
    QString text = item->text();
    QString user = text.section("  ", 0, 0).trimmed();
    if (user.isEmpty() || user == username_) return;

    // Switch to DM mode targeting this user
    modeCombo_->setCurrentIndex(1);
    int idx = dmTargetCombo_->findText(user);
    if (idx >= 0) dmTargetCombo_->setCurrentIndex(idx);

    // Create/switch to DM tab
    getOrCreateDmTab(user);
    int tabIdx = chatTabs_->indexOf(dmTabs_[user]);
    if (tabIdx >= 0) chatTabs_->setCurrentIndex(tabIdx);

    messageInput_->setFocus();
}

void MainWindow::onShowUserInfo() {
    bool ok;
    QString target = QInputDialog::getText(this, "Información de Usuario",
                                           "Nombre de usuario:", QLineEdit::Normal,
                                           "", &ok);
    if (ok && !target.trimmed().isEmpty()) {
        net_sender_->sendGetUserInfo(target.trimmed().toStdString(),
                                      username_.toStdString(), local_ip_);
    }
}

void MainWindow::onShowHelp() {
    QMessageBox::information(this, "Ayuda - Chat Client",
        "<h3>Chat Client - Ayuda</h3>"
        "<p><b>Chat General:</b> Selecciona modo 'General' y escribe tu mensaje.</p>"
        "<p><b>Mensaje Directo:</b> Selecciona modo 'DM', elige el destinatario y envía.</p>"
        "<p><b>Doble clic</b> en un usuario de la lista para iniciar DM.</p>"
        "<p><b>Cambiar Estado:</b> Usa el selector en la barra superior.</p>"
        "<ul>"
        "<li>🟢 ACTIVO - Disponible</li>"
        "<li>🔴 OCUPADO - No molestar</li>"
        "<li>⚫ INACTIVO - Invisible/ausente</li>"
        "</ul>"
        "<p><b>🔄 Usuarios:</b> Actualizar lista de usuarios conectados.</p>"
        "<p><b>ℹ️ Info Usuario:</b> Ver IP, estado y nombre de un usuario.</p>"
        "<p><b>Cerrar ventana:</b> Se desconecta del servidor.</p>"
        "<hr>"
        "<p><i>Protocolo: TCP + Protocol Buffers + framing 5 bytes</i></p>"
    );
}

void MainWindow::onModeChanged(int index) {
    dmTargetCombo_->setVisible(index == 1);
    if (index == 0) {
        messageInput_->setPlaceholderText("Escribe un mensaje...");
    } else {
        messageInput_->setPlaceholderText("Mensaje directo...");
    }
}

// ============================================================
// Network Signal Slots
// ============================================================

void MainWindow::onServerResponse(int statusCode, QString message, bool isSuccessful) {
    QString color = isSuccessful ? "#a6e3a1" : "#f38ba8";
    appendSystemMessage("<span style='color:" + color + ";'>[Código " +
                        QString::number(statusCode) + "] " +
                        message.toHtmlEscaped() + "</span>");

    // Handle specific status codes
    switch (statusCode) {
        case StatusCode::OK_STATUS_CHANGED:
        case StatusCode::STATUS_RESTORED: {
            // Refresh the status combo to reflect the current status
            // We need to request user list to see our own status, or parse the message
            // For simplicity, request a refresh
            onRefreshUsers();

            if (statusCode == StatusCode::AUTO_INACTIVE) {
                // Server auto-set us to INACTIVE
                current_status_ = chat::INVISIBLE;
                statusCombo_->blockSignals(true);
                statusCombo_->setCurrentIndex(2);  // INACTIVO
                statusCombo_->blockSignals(false);
            } else if (statusCode == StatusCode::STATUS_RESTORED) {
                current_status_ = chat::ACTIVE;
                statusCombo_->blockSignals(true);
                statusCombo_->setCurrentIndex(0);  // ACTIVO
                statusCombo_->blockSignals(false);
            } else if (statusCode == StatusCode::OK_STATUS_CHANGED) {
                // Update local status based on what was set
                int idx = statusCombo_->currentIndex();
                current_status_ = static_cast<chat::StatusEnum>(
                    statusCombo_->itemData(idx).toInt());
            }
            break;
        }
        case StatusCode::AUTO_INACTIVE: {
            current_status_ = chat::INVISIBLE;
            statusCombo_->blockSignals(true);
            statusCombo_->setCurrentIndex(2);
            statusCombo_->blockSignals(false);
            break;
        }
        default:
            break;
    }
}

void MainWindow::onAllUsers(QStringList usernames, QVector<int> statuses) {
    userList_->clear();
    dmTargetCombo_->clear();

    for (int i = 0; i < usernames.size(); ++i) {
        QString name = usernames[i];
        int st = (i < statuses.size()) ? statuses[i] : 0;
        QString display = name + "  " + statusToDisplay(st);
        userList_->addItem(display);

        // Don't add ourselves to DM target list
        if (name != username_) {
            dmTargetCombo_->addItem(name);
        }
    }
}

void MainWindow::onForDm(QString senderUsername, QString message) {
    appendDmChat(senderUsername, message, true);
    appendSystemMessage("<span style='color:#cba6f7;'>📩 DM recibido de " +
                        senderUsername.toHtmlEscaped() + "</span>");
}

void MainWindow::onBroadcast(QString usernameOrigin, QString message) {
    appendGeneralChat(usernameOrigin, message);
}

void MainWindow::onUserInfo(QString ipAddress, QString username, int status) {
    QString statusStr = statusToDisplay(status);
    QMessageBox::information(this, "Información de Usuario",
        "<h3>👤 " + username.toHtmlEscaped() + "</h3>"
        "<p><b>IP:</b> " + ipAddress.toHtmlEscaped() + "</p>"
        "<p><b>Estado:</b> " + statusStr + "</p>"
    );
}

void MainWindow::onConnectionLost() {
    statusLabel_->setText("  🔴 Desconectado  ");
    statusLabel_->setStyleSheet("color: #f38ba8; font-weight: bold;");
    appendSystemMessage("<span style='color:#f38ba8;'><b>❌ Conexión perdida con el servidor.</b></span>");

    // Disable input
    messageInput_->setEnabled(false);
    sendButton_->setEnabled(false);
    statusCombo_->setEnabled(false);
}

// ============================================================
// Close event – clean shutdown
// ============================================================

void MainWindow::closeEvent(QCloseEvent* event) {
    // Send Quit message
    net_sender_->sendQuit(local_ip_);

    // Stop network thread
    if (net_thread_) {
        net_thread_->stopThread();
        net_thread_->wait(2000);
    }

    // Close socket
    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
    }

    event->accept();
}
