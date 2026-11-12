#include "network.h"
#include <stdexcept>
#include <string>

using namespace std; 

Network::Network() {
    WSADATA data{}; //Fills info

    const int error = WSAStartup(MAKEWORD(2, 2), &data); //starts winsock in 2.2 version and gives windows the memory add
    
    if (error != 0) { // != 0 as winsock returns 0 if no error
        throw runtime_error(
            "Winsock startup failed: " + to_string(error)
        );
    }
}

Network::~Network() {
    //runs automatically when the Network object leaves its scope
    WSACleanup();
}

Socket::Socket(SOCKET socket_handle): handle(socket_handle) {
    if (handle == INVALID_SOCKET) { //INVALID_SOCKET is a windows method to indicate that socket creation failed
        throw runtime_error(
            "Socket operation failed: " +
            to_string(WSAGetLastError()) //most recent windows error
        );
    }
}

Socket::~Socket() {
    //release this socket when its owning object is finished
    closesocket(handle);
}

SOCKET Socket::get() const {
    return handle;
}

//sending all message bytes
void send_all(SOCKET socket, const char* data, int size) {
    int sent = 0;

    while (sent < size) {
        //skip bytes already sent and request only the remaining bytes
        const int count = send(socket, data + sent, size - sent, 0); //counts how many bytes were sent from this send function
        if (count == SOCKET_ERROR) {
            throw runtime_error( "Send failed: " + to_string(WSAGetLastError()));
        }

        if (count == 0) {
            //avoid looping forever if send makes no progress
            throw runtime_error("Send made no progress.");
        }

        sent += count; //how many bytes sent total 
    }
}

//receives the exact message bytes
bool receive_exact(SOCKET socket, char* data, int size) {
    int received = 0;

    while (received < size) {
        //fill the next empty position without overwriting earlier bytes
        const int count = recv( socket, data + received, size - received, 0);

        if (count == SOCKET_ERROR) {
            throw runtime_error(
                "Receive failed: " + to_string(WSAGetLastError())
            );
        }

        if (count == 0) {
            //zero means the peer has closed its sending side
            if (received == 0) {
                return false;
            }

            //some bytes arrived, so this is an incomplete read
            throw runtime_error(
                "Connection closed before all expected bytes arrived.");
        }
        received += count; //counts how many received from this loop
    }

    return true;
}
