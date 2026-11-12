//pragma tells how many times to compile this program
#pragma once
#include <cstdint>
#include <string> 
#include <array>
#include "network.h"

using namespace std; 
//Structure of header 
enum class MessageType : uint8_t {
    //these numbers must mean the same thing on both sides
    Text = 1,
    Ping = 2,
    Pong = 3,
    Goodbye = 4,
    Error = 5
};

//max size of payload we send, constexpr means value is knowing during compile
constexpr uint32_t max_payload = 65536;

//kind of message and the actual message
struct Message {
    MessageType type;
    string payload;
}; 

//holds expected length of payload
struct MessageHeader {
    MessageType type;
    uint32_t payload_length;
};

//Functions for protocol.cpp
    //encoding
array<uint8_t, 8> encode_header(const Message& message);
    //accepts 8 bytes (by ref) and is unchangable
MessageHeader decode_header(
    const array<uint8_t, 8>& header
); 

//send one whole message using our header format
void send_message(SOCKET socket, const Message& message);

//fill message only after its header and payload have arrived
bool receive_message(SOCKET socket, Message& message);
