// main.cpp – Client entry point.
// Usage: ./chat_client <username> <server_ip> <server_port>

#include <QApplication>
#include <QMessageBox>
#include <iostream>
#include <cstring>
#include <cstdlib>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "chat/protocol.h"
#include "mainwindow.h"
#include "network.h"

#include "register.pb.h"
#include "server_response.pb.h"

#include <google/protobuf/stubs/common.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Chat Client");

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <username> <server_ip> <server_port>" << std::endl;
        return 1;
    }

    std::string username  = argv[1];
    std::string server_ip = argv[2];
    uint16_t server_port  = static_cast<uint16_t>(std::atoi(argv[3]));

    // Create TCP socket
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        QMessageBox::critical(nullptr, "Error", "No se pudo crear el socket.");
        return 1;
    }

    // Connect to server
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(server_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        QMessageBox::critical(nullptr, "Error",
                              QString("Dirección IP inválida: %1").arg(QString::fromStdString(server_ip)));
        ::close(sockfd);
        return 1;
    }

    if (::connect(sockfd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        QMessageBox::critical(nullptr, "Error",
                              QString("No se pudo conectar a %1:%2\n%3")
                              .arg(QString::fromStdString(server_ip))
                              .arg(server_port)
                              .arg(strerror(errno)));
        ::close(sockfd);
        return 1;
    }

    // Get local IP used for this connection
    std::string local_ip = get_local_ip(sockfd);
    std::cout << "[CLIENT] Connected to " << server_ip << ":" << server_port
              << " (local IP: " << local_ip << ")" << std::endl;

    // Send Register message
    chat::Register reg;
    reg.set_username(username);
    reg.set_ip(local_ip);
    if (!send_message(sockfd, MsgType::REGISTER, reg)) {
        QMessageBox::critical(nullptr, "Error", "Error enviando registro al servidor.");
        ::close(sockfd);
        return 1;
    }

    // Wait for ServerResponse
    uint8_t resp_type;
    std::string resp_payload;
    if (!recv_message(sockfd, resp_type, resp_payload)) {
        QMessageBox::critical(nullptr, "Error", "No se recibió respuesta del servidor.");
        ::close(sockfd);
        return 1;
    }

    if (resp_type == MsgType::SERVER_RESPONSE) {
        chat::ServerResponse resp;
        if (resp.ParseFromString(resp_payload)) {
            if (!resp.is_successful()) {
                QMessageBox::critical(nullptr, "Registro Fallido",
                                      QString("[Código %1] %2")
                                      .arg(resp.status_code())
                                      .arg(QString::fromStdString(resp.message())));
                ::close(sockfd);
                return 1;
            }
            std::cout << "[CLIENT] Registered successfully: " << resp.message() << std::endl;
        }
    } else {
        QMessageBox::critical(nullptr, "Error", "Respuesta inesperada del servidor.");
        ::close(sockfd);
        return 1;
    }

    // Show main window
    MainWindow window(QString::fromStdString(username), sockfd, local_ip);
    window.show();

    int result = app.exec();

    google::protobuf::ShutdownProtobufLibrary();
    return result;
}
