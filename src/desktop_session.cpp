//the GUI uses two threads, but messages still travel through real TCP sockets
#define NOMINMAX
#include "desktop_session.h"
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <stdexcept>

namespace {
thread serverThread, clientThread;
mutex sessionMutex;
condition_variable changed;
atomic<bool> stopping{false};
bool listening = false, failed = false;
SOCKET clientSocket = INVALID_SOCKET, serverSocket = INVALID_SOCKET;
queue<string> commands;
vector<DesktopUpdate> updates;
thread_local bool serverSide = false;

void update(UpdateType type, const string& text = "") {
    DesktopUpdate item{};
    item.type = type; item.server = serverSide; item.text = text;
    lock_guard<mutex> lock(sessionMutex);
    updates.push_back(item);
}

//keep shutdown from using a socket that its worker has already closed
struct ActiveSocket {
    SOCKET& shared;
    ActiveSocket(SOCKET& shared, SOCKET handle) : shared(shared) {
        lock_guard<mutex> lock(sessionMutex);
        shared = handle;
    }
    ~ActiveSocket() {
        lock_guard<mutex> lock(sessionMutex);
        shared = INVALID_SOCKET;
    }
};

void server_loop() {
    serverSide = true;
    try {
        Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(54000);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
            || listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) {
            throw runtime_error("Cannot listen on port 54000: " + to_string(WSAGetLastError()));
        }
        update(UpdateType::Log, "Listening on port 54000.\r\n");
        { lock_guard<mutex> lock(sessionMutex); listening = true; }
        changed.notify_all();
        update(UpdateType::Ready);

        //check every 100 ms so closing the app also stops a waiting server
        while (!stopping) {
            fd_set sockets; FD_ZERO(&sockets); FD_SET(listener.get(), &sockets);
            timeval timeout{0, 100000};
            int result = select(0, &sockets, nullptr, nullptr, &timeout);
            if (result == SOCKET_ERROR) throw runtime_error("Listener failed.");
            if (result == 0) continue;
            Socket peer(accept(listener.get(), nullptr, nullptr));
            ActiveSocket active(serverSocket, peer.get());
            if (stopping) break;
            update(UpdateType::Log, "Client connected.\r\n");
            Message message{};
            while (!stopping && receive_message(peer.get(), message)) {
                if (message.type == MessageType::Text) {
                    update(UpdateType::Log, "Text (" + to_string(message.payload.size()) + " bytes): " + message.payload + "\r\n");
                    send_message(peer.get(), message);
                } else if (message.type == MessageType::Ping) {
                    update(UpdateType::Log, "Ping received; sending Pong.\r\n");
                    send_message(peer.get(), {MessageType::Pong, ""});
                } else if (message.type == MessageType::Goodbye) {
                    send_message(peer.get(), {MessageType::Goodbye, ""});
                    break;
                } else send_message(peer.get(), {MessageType::Error, "Client may send Text, Ping or Goodbye."});
            }
            break;
        }
    } catch (const exception& error) {
        if (!stopping) update(UpdateType::Log, string("Error: ") + error.what() + "\r\n");
    }
    { lock_guard<mutex> lock(sessionMutex); failed = !listening; }
    changed.notify_all();
    update(UpdateType::Done, "Server stopped.\r\n");
}

void client_loop() {
    try {
        {
            unique_lock<mutex> lock(sessionMutex);
            changed.wait(lock, [] { return listening || failed || stopping; });
            if (stopping || failed) return;
        }
        Socket peer(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        ActiveSocket active(clientSocket, peer.get());
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(54000);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (stopping) return;
        if (connect(peer.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            throw runtime_error("Connection failed: " + to_string(WSAGetLastError()));
        }
        update(UpdateType::Log, "Connected to 127.0.0.1:54000\r\nText, /ping or /quit.\r\n> ");
        update(UpdateType::Ready);
        while (!stopping) {
            string input;
            {
                unique_lock<mutex> lock(sessionMutex);
                changed.wait(lock, [] { return !commands.empty() || stopping; });
                if (stopping) break;
                input = commands.front(); commands.pop();
            }
            Message message{MessageType::Text, input};
            if (input == "/ping") message = {MessageType::Ping, ""};
            if (input == "/quit") message = {MessageType::Goodbye, ""};
            send_message(peer.get(), message);
            Message reply{};
            if (!receive_message(peer.get(), reply)) throw runtime_error("Server disconnected before replying.");
            if (reply.type == MessageType::Goodbye && message.type == MessageType::Goodbye) {
                update(UpdateType::Log, "Conversation closed.\r\n"); break;
            }
            if (reply.type == MessageType::Text && message.type == MessageType::Text)
                update(UpdateType::Log, "Server echoed: " + reply.payload + "\r\n> ");
            else if (reply.type == MessageType::Pong && message.type == MessageType::Ping)
                update(UpdateType::Log, "Pong received.\r\n> ");
            else throw runtime_error(reply.type == MessageType::Error ? reply.payload : "Unexpected reply type.");
            update(UpdateType::Ready);
        }
    } catch (const exception& error) {
        if (!stopping) update(UpdateType::Log, string("Error: ") + error.what() + "\r\n");
    }
    update(UpdateType::Done, "Client stopped.\r\n");
}
}

void stop_desktop_session() {
    stopping = true;
    {   //wake any blocking receives before waiting for the threads
        lock_guard<mutex> lock(sessionMutex);
        if (clientSocket != INVALID_SOCKET) shutdown(clientSocket, SD_BOTH);
        if (serverSocket != INVALID_SOCKET) shutdown(serverSocket, SD_BOTH);
    }
    changed.notify_all();
    if (clientThread.joinable()) clientThread.join();
    if (serverThread.joinable()) serverThread.join();
}

void start_desktop_session() {
    stop_desktop_session();
    { lock_guard<mutex> lock(sessionMutex); updates.clear(); commands = {}; listening = failed = false; }
    stopping = false;
    try {
        serverThread = thread(server_loop);
        clientThread = thread(client_loop);
    } catch (...) {
        stop_desktop_session();
        throw;
    }
}

void send_desktop_command(const string& command) {
    { lock_guard<mutex> lock(sessionMutex); commands.push(command); }
    changed.notify_all();
}

vector<DesktopUpdate> read_desktop_updates() {
    vector<DesktopUpdate> result;
    lock_guard<mutex> lock(sessionMutex);
    result.swap(updates);
    return result;
}

void desktop_trace(bool outgoing, const unsigned char* header, const char* payload, unsigned int length) {
    thread_local unsigned int sent = 0, received = 0;
    DesktopUpdate item{};
    item.type = UpdateType::Frame; item.server = serverSide; item.outgoing = outgoing;
    item.sequence = outgoing ? ++sent : ++received;
    copy(header, header + 8, item.header.begin());
    item.preview.assign(payload, min(length, 64U));
    lock_guard<mutex> lock(sessionMutex);
    updates.push_back(item);
}
