// main.cpp – Server entry point.
// Usage: ./chat_server <port> [--idle-timeout <seconds>]

#include "chat/server.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <google/protobuf/stubs/common.h>

static ChatServer* g_server = nullptr;

static void signal_handler(int) {
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <port> [--idle-timeout <seconds>]" << std::endl;
        return 1;
    }

    uint16_t port = static_cast<uint16_t>(std::atoi(argv[1]));
    int idle_timeout = 180;  // default

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--idle-timeout") == 0 && i + 1 < argc) {
            idle_timeout = std::atoi(argv[++i]);
            if (idle_timeout <= 0) {
                std::cerr << "Invalid idle timeout. Using default (180s)." << std::endl;
                idle_timeout = 180;
            }
        }
    }

    // Setup signal handlers for clean shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    ChatServer server(port, idle_timeout);
    g_server = &server;

    server.run();

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
