#pragma once

#include "protocol.h"
#include <vector>

//the worker threads send these updates to the window
enum class UpdateType { Log, Ready, Frame, Done };

struct DesktopUpdate {
    UpdateType type;
    bool server = false;
    bool outgoing = false;
    unsigned int sequence = 0;
    string text;
    array<unsigned char, 8> header{};
    string preview;
};

void start_desktop_session();
void stop_desktop_session();
void send_desktop_command(const string& command);
vector<DesktopUpdate> read_desktop_updates();
