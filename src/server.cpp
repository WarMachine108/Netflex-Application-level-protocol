#include "protocol.h"
#include <bits/stdc++.h>
#include <stdexcept>

using namespace std; 

int main() {
    try {
        Network network; 
        Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); //listening socket
        sockaddr_in address{}; //ipv4 socket addr structure initialised to zero from {}

        address.sin_family = AF_INET; //ipv4
        address.sin_port = htons(54000); //port no
        address.sin_addr.s_addr = htonl(INADDR_ANY); //lets server listen on all local ipv4 areas (via INADDR), htonl is to convert into big-endian (shifting thingy)

        if (bind( //fix socket
                listener.get(),
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)
            ) == SOCKET_ERROR) {
            throw runtime_error(
                "Bind failed: " + to_string(WSAGetLastError())
            );
        }

        //start queuing incoming connection requests
        if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) { 
            throw runtime_error(
                "Listen failed: " + to_string(WSAGetLastError())
            );
        }
        //tell the dashboard when the server is actually listening
#ifdef NETFLEX_DESKTOP
        desktop_server_ready();
#endif
        cout << "NetFlex server: waiting on port 54000..." << endl;
        //wait for a client, accept gives a new socket for this conversation
        Socket client(accept(listener.get(), nullptr, nullptr));

        cout << "Client connected.\n";
        Message message{};

        //read complete messages until the client leaves or says goodbye
        while (receive_message(client.get(), message)) {
            if (message.type == MessageType::Text) {
                cout << "Text (" << message.payload.size()
                          << " bytes): " << message.payload << '\n';
                //echo the same text back
                send_message(client.get(), message);
            } else if (message.type == MessageType::Ping) {
                //pong tells the client we are responding
                cout << "Ping received; sending Pong.\n";
                send_message(client.get(), {MessageType::Pong, ""});
            } else if (message.type == MessageType::Goodbye) {
                //reply before ending the conversation
                send_message(client.get(), {MessageType::Goodbye, ""});
                cout << "Goodbye received; closing conversation.\n";
                break;
            } else {
                //pong and error are not supported client requests
                send_message(client.get(), {
                    MessageType::Error,
                    "Client may send Text, Ping, or Goodbye."
                });
            }
        }

        cout << "Client disconnected.\n";
    } catch (const exception& error) {
        cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
