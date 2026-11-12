#include "protocol.h"
#include <stdexcept> //report problems
#include <bits/stdc++.h>

using namespace std; 
//function linked with protocol.h, value stored in 32 bit integer
//32 bit length takes 4 bytes -> So we do shifting to separate the 4 bytes i.e XX XX XX XX -> in hexa
array<uint8_t, 8> encode_header(const Message& message) {
    //check size before converting it to a 32 bit number
    if (message.payload.size() > max_payload) {
        throw runtime_error("Payload exceeds 65536 bytes.");
    }
    //static cast converts value to unsigned int 32 bit
    const uint32_t length =
        static_cast<uint32_t>(message.payload.size());

    //8 header bytes, payload length is written biggest byte first
    return {
        'N', //0 bit
        'F', //1 bit
        1, //version
        static_cast<uint8_t>(message.type), //Msg type no
        //payload length
        static_cast<uint8_t>(length >> 24), 
        static_cast<uint8_t>(length >> 16),
        static_cast<uint8_t>(length >> 8),
        static_cast<uint8_t>(length)
    };
}

//returns checked message header
MessageHeader decode_header(
    const array<uint8_t, 8>& header
) {
    //check marker, version and type before trusting the header
    if (header[0] != 'N' || header[1] != 'F') {
        throw runtime_error("Invalid NetFlex marker.");
    }

    if (header[2] != 1) {
        throw runtime_error("Unsupported protocol version.");
    }

    if (header[3] < 1 || header[3] > 5) {
        throw runtime_error("Unsupported message type.");
    }

    const MessageType type =
        static_cast<MessageType>(header[3]);

    //shifted gets unshifted , we do OR to combined bits into 1 number and convert into 32 bit unsign int
    const uint32_t length =
        (static_cast<uint32_t>(header[4]) << 24) |
        (static_cast<uint32_t>(header[5]) << 16) |
        (static_cast<uint32_t>(header[6]) << 8) |
        static_cast<uint32_t>(header[7]);

    if (length > max_payload) {
        throw runtime_error("Payload exceeds 65536 bytes.");
    }

    //these types already express the command, so need no payload
    if ((type == MessageType::Ping ||
         type == MessageType::Pong ||
         type == MessageType::Goodbye) && length != 0) {
        throw runtime_error("Control messages must have no payload.");
    }

    return {type, length};
}

void send_message(SOCKET socket, const Message& message) {
    const auto header = encode_header(message);
    //use the same validation rules for outgoing messages
    decode_header(header);

    //send the fixed header first
    send_all(
        socket,
        reinterpret_cast<const char*>(header.data()),
        static_cast<int>(header.size())
    );
    //then send the actual message content
    send_all(
        socket,
        message.payload.data(),
        static_cast<int>(message.payload.size())
    );
#ifdef NETFLEX_DESKTOP
    desktop_trace(true, header.data(), message.payload.data(), static_cast<unsigned int>(message.payload.size()));
#endif
}

bool receive_message(SOCKET socket, Message& message) {
    //read exactly 8 bytes before finding the payload length
    array<uint8_t, 8> header{};

    if (!receive_exact(
            socket,
            reinterpret_cast<char*>(header.data()),
            static_cast<int>(header.size())
        )) {
        return false;
    }

    const MessageHeader decoded = decode_header(header);

    //allocate only after the received length has been checked
    string payload(decoded.payload_length, '\0');

    if (!receive_exact(
            socket,
            payload.data(),
            static_cast<int>(payload.size())
        )) {
        throw runtime_error(
            "Connection closed before the payload arrived."
        );
    }

    //give the caller the complete message
    message = {decoded.type, payload};

#ifdef NETFLEX_DESKTOP
    desktop_trace(false, header.data(), payload.data(), static_cast<unsigned int>(payload.size()));
#endif

    return true;
}
