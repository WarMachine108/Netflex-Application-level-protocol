#include "protocol.h"
#include <bits/stdc++.h>
#include <exception>
#include <stdexcept>

using namespace std; 
int main() {
    try {
        //start windows networking
        Network network;
        Socket server(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); //ipv4 addr, stream of bytes, use TCP
        
        sockaddr_in address{}; //same as server        
        address.sin_family = AF_INET;
        address.sin_port = htons(54000);

        if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) { //inet_pton converts IP from text to binary
            throw runtime_error("Invalid server IPv4 address.");
        }

        //connect this socket to the server address
        if (connect(
                server.get(),
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)
            ) == SOCKET_ERROR) {
            throw runtime_error(
                "Connection failed: " + to_string(WSAGetLastError())
            );
        }
        //tell the dashboard only after connect actually succeeds
#ifdef NETFLEX_DESKTOP
        desktop_client_ready();
#endif
        cout << "Connected to Netflex server.\n";
        cout << "Enter text, /ping, or /quit.\n";

        //keep using the same connection for more messages
        while (true) {
            cout << "> ";

            string input;
            //if input ends, close the conversation normally
            if (!getline(cin, input)) {
                input = "/quit";
            }

            //reject long input before sending anything
            if (input.size() > max_payload) {
                cout << "Message too long. Maximum: 65536 bytes.\n";
                continue;
            }

            //normal text by default, commands change the message type
            Message message{MessageType::Text, input};
            if (input == "/ping") {
                message = {MessageType::Ping, ""};
            } else if (input == "/quit") {
                message = {MessageType::Goodbye, ""};
            }

            //send header and payload, then wait for one reply
            send_message(server.get(), message);

            Message reply{};
            if (!receive_message(server.get(), reply)) {
                throw runtime_error("Server disconnected before replying.");
            }

            //check that the reply matches what we sent
            if (reply.type == MessageType::Error) {
                throw runtime_error("Server error: " + reply.payload);
            } else if (message.type == MessageType::Text &&
                       reply.type == MessageType::Text) {
                cout << "Server echoed: " << reply.payload << '\n';
            } else if (message.type == MessageType::Ping &&
                       reply.type == MessageType::Pong) {
                cout << "Pong received.\n";
            } else if (message.type == MessageType::Goodbye &&
                       reply.type == MessageType::Goodbye) {
                cout << "Conversation closed.\n";
                break;
            } else {
                throw runtime_error("Unexpected reply type.");
            }
        }
    } catch (const exception& error) {
        //show the error instead of leaving it unhandled
        cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}

