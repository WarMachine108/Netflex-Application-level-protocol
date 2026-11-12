#pragma once

//networking libraries of windows
#include <winsock2.h>
#include <ws2tcpip.h>

//starts winsock, cleans it when object finished
class Network {
public:
    Network();
    ~Network();

    Network(const Network&) = delete; //prevents copying, & for reference and delete prevnts the operating to occur
    Network& operator=(const Network&) = delete; //prevents variable copy i.e 2nd net = 1st net
};

//socket for client to communicate with the server
//SOCKET -> Windows provided, Socket -> made by me

class Socket {
public:
    explicit Socket(SOCKET socket_handle); //explicit prevents converting raw windows handle to our own object
    ~Socket();

    SOCKET get() const; //returns the stored handle

    //prevents copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

private:
    //windows uses this handle to identify the socket
    SOCKET handle;
} ;

//data points to the bytes, size is how many to send
void send_all(SOCKET socket, const char* data, int size); 
//false means the peer closed before any bytes for this read arrived
bool receive_exact(SOCKET socket, char* data, int size);

//only the desktop build uses these to update its status lights
#ifdef NETFLEX_DESKTOP
void desktop_server_ready();
void desktop_client_ready();
//show actual completed message sends and receives in the app
void desktop_trace(bool outgoing, const unsigned char* header, const char* payload, unsigned int length);
#endif
