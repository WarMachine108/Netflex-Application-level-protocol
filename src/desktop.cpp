//window controls, commands and the byte display
#define NOMINMAX
#include "desktop_session.h"
#include <windows.h>
#include <dwmapi.h>
#include <richedit.h>
#include <algorithm>
#include <deque>
#include <iomanip>
#include <sstream>
#include <stdexcept>

using namespace std;
const COLORREF background = RGB(30, 30, 30), terminalColor = RGB(20, 20, 20);
const COLORREF textColor = RGB(238, 238, 238), clientColor = RGB(156, 193, 222), serverColor = RGB(184, 205, 156);
HWND window, clientLog, serverLog, clientInput, serverInput, frameList, statusLabel, counters;
HWND sendButton, demoButton, replayButton, description, payloadLabel, receivedLabel, headerCells[8];
HFONT normalFont, monoFont;
HBRUSH backgroundBrush, terminalBrush;
WNDPROC originalInput;
bool ready = false, connected = false, serverReady = false, followLatest = true;
vector<wstring> history;
size_t historyPosition = 0;
deque<wstring> demoCommands;
unsigned int sent = 0, received = 0;
unsigned long long byteCount = 0;
enum { ClientInput = 201, ServerInput, Send, Run, Restart, Demo, Replay, Frames, ClientLog, ServerLog };

struct Placement { HWND control; int x, y, width, height; };
vector<Placement> controls;
struct Frame { DesktopUpdate data; bool sent = false, received = false; };
vector<Frame> frames;
int selected = -1, animation = -1;
ULONGLONG animationStart = 0;

//convert between Windows text and the UTF-8 bytes sent by NetFlex
wstring wide(const string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
string utf8(const wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
wstring read_text(HWND control) {
    int size = GetWindowTextLengthW(control);
    wstring text(size + 1, L'\0'); GetWindowTextW(control, text.data(), size + 1); text.resize(size);
    return text;
}
void set_text(HWND control, const wstring& text) { SetWindowTextW(control, text.c_str()); }
void append(HWND log, const wstring& text) {
    if (GetWindowTextLengthW(log) > 100000) {
        SendMessageW(log, EM_SETSEL, 0, 50000); SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    }
    SendMessageW(log, EM_SETSEL, -1, -1);
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(log, EM_SCROLLCARET, 0, 0);
}
int sx(int x) { RECT r{}; GetClientRect(window, &r); return MulDiv(x, r.right, 1200); }
int sy(int y) { RECT r{}; GetClientRect(window, &r); return MulDiv(y, r.bottom, 840); }
void layout() {
    for (const auto& item : controls)
        MoveWindow(item.control, sx(item.x), sy(item.y), sx(item.width), sy(item.height), TRUE);
}
HWND control(const wchar_t* kind, const wchar_t* name, DWORD style, int id, int x, int y, int w, int h) {
    HWND result = CreateWindowW(kind, name, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    controls.push_back({result, x, y, w, h});
    bool textBox = wstring(kind) == L"EDIT" || wstring(kind) == L"RICHEDIT50W";
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(textBox || id == Frames ? monoFont : normalFont), FALSE);
    if (wstring(kind) == L"RICHEDIT50W") {
        SendMessageW(result, EM_SETBKGNDCOLOR, 0, terminalColor);
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format); format.dwMask = CFM_COLOR; format.crTextColor = textColor;
        SendMessageW(result, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
        SendMessageW(result, EM_SETCHARFORMAT, 0, reinterpret_cast<LPARAM>(&format));
    }
    return result;
}
HWND label(const wchar_t* text, int x, int y, int w, int h = 22) { return control(L"STATIC", text, 0, 0, x, y, w, h); }
HWND button(const wchar_t* text, int id, int x, int y, int w) { return control(L"BUTTON", text, WS_TABSTOP, id, x, y, w, 32); }
void buttons() {
    EnableWindow(sendButton, ready && connected);
    EnableWindow(demoButton, ready && connected && demoCommands.empty());
    EnableWindow(replayButton, selected >= 0);
}
unsigned int length_of(const Frame& frame) {
    const auto& h = frame.data.header;
    return (unsigned(h[4]) << 24) | (unsigned(h[5]) << 16) | (unsigned(h[6]) << 8) | h[7];
}
wstring title(const Frame& frame) {
    const wchar_t* types[] = {L"?", L"TEXT", L"PING", L"PONG", L"GOODBYE", L"ERROR"};
    return L"#" + to_wstring(frame.data.sequence) + (frame.data.outgoing != frame.data.server ? L" C>S  " : L" S>C  ")
        + types[min(5, int(frame.data.header[3]))] + L"  " + to_wstring(8ULL + length_of(frame)) + L" B";
}
wstring hex_bytes(const unsigned char* data, size_t size) {
    wostringstream output;
    for (size_t i = 0; i < size; ++i) output << uppercase << setfill(L'0') << setw(2) << hex << unsigned(data[i]) << L' ';
    return output.str();
}
void inspect() {
    if (selected < 0) return;
    const Frame& frame = frames[selected]; set_text(description, title(frame));
    for (int i = 0; i < 8; ++i) set_text(headerCells[i], hex_bytes(&frame.data.header[i], 1));
    set_text(receivedLabel, frame.received ? L"Received / valid header" : L"Sent");
    string preview = frame.data.preview;
    wstring content = hex_bytes(reinterpret_cast<const unsigned char*>(preview.data()), min(size_t(16), preview.size()));
    if (preview.size() > 16) content += L"...";
    for (char& byte : preview) if (static_cast<unsigned char>(byte) < 32 || static_cast<unsigned char>(byte) > 126) byte = '.';
    set_text(payloadLabel, to_wstring(length_of(frame)) + L" payload bytes\r\n" + content + L"\r\n" + wide(preview));
}
void capture(const DesktopUpdate& data) {
    int index = -1;
    for (size_t i = 0; i < frames.size(); ++i)
        if (frames[i].data.sequence == data.sequence && (frames[i].data.outgoing != frames[i].data.server) == (data.outgoing != data.server)) index = static_cast<int>(i);
    if (index < 0) {
        if (frames.size() == 256) { frames.erase(frames.begin()); --selected; --animation; SendMessageW(frameList, LB_DELETESTRING, 0, 0); }
        frames.push_back({data}); index = static_cast<int>(frames.size()) - 1;
        wstring name = title(frames.back()); SendMessageW(frameList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (animation < 0) { animation = 0; animationStart = GetTickCount64(); }
        if (followLatest) { selected = index; SendMessageW(frameList, LB_SETCURSEL, selected, 0); }
    }
    Frame& frame = frames[index];
    if (data.outgoing && !frame.sent) { frame.sent = true; ++sent; byteCount += 8ULL + length_of(frame); }
    if (!data.outgoing && !frame.received) { frame.received = true; ++received; }
    set_text(counters, L"Sent: " + to_wstring(sent) + L"  Received: " + to_wstring(received) + L"  Bytes: " + to_wstring(byteCount));
    inspect();
}
void restart() {
    ready = connected = serverReady = false; demoCommands.clear(); frames.clear(); selected = animation = -1;
    sent = received = 0; byteCount = 0; followLatest = true;
    SendMessageW(frameList, LB_RESETCONTENT, 0, 0); set_text(clientLog, L""); set_text(serverLog, L"");
    set_text(statusLabel, L"Starting server..."); set_text(counters, L"Sent: 0  Received: 0  Bytes: 0");
    set_text(description, L"Select a message to see its bytes."); set_text(payloadLabel, L""); set_text(receivedLabel, L"");
    for (HWND cell : headerCells) set_text(cell, L"--");
    start_desktop_session(); buttons();
}
bool send_input(const wstring& input) {
    if (input == L"/clear" || input == L"clear") { set_text(clientLog, L"> "); return true; }
    if (input == L"/help" || input == L"help") {
        append(clientLog, L"\r\nText: echo | /ping: pong | /quit: close\r\n/demo: sample | /clear: clear | Up/Down: history\r\n> "); return true;
    }
    if (!ready || !connected) return false;
    if (input == L"/demo") { demoCommands = {L"Hello NetFlex!", L"/ping", L"Every byte has a meaning."}; return true; }
    string bytes = utf8(input);
    if (bytes.size() > max_payload) { append(clientLog, L"\r\nMessage exceeds 65,536 bytes.\r\n> "); return false; }
    append(clientLog, input + L"\r\n"); send_desktop_command(bytes);
    history.push_back(input); historyPosition = history.size(); ready = false; followLatest = true; buttons(); return true;
}
void submit(bool clientSide) {
    HWND input = clientSide ? clientInput : serverInput; wstring text = read_text(input);
    if (clientSide) { if (send_input(text)) set_text(input, L""); return; }
    set_text(input, L""); append(serverLog, L"\r\nserver> " + text + L"\r\n");
    if (text == L"/restart") restart();
    else if (text == L"/clear") set_text(serverLog, L"");
    else if (text == L"/status") append(serverLog, wstring(L"Server: ") + (serverReady ? L"ready" : L"stopped") + L" | Client: " + (connected ? L"connected\r\n" : L"offline\r\n"));
    else append(serverLog, L"Local commands: /status, /clear, /restart, /help\r\n");
}
LRESULT CALLBACK input_proc(HWND input, UINT message, WPARAM key, LPARAM data) {
    if (message == WM_GETDLGCODE && key == VK_RETURN) return DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN && key == VK_RETURN) { submit(input == clientInput); return 0; }
    if (message == WM_CHAR && key == VK_RETURN) return 0;
    if (message == WM_KEYDOWN && input == clientInput && (key == VK_UP || key == VK_DOWN)) {
        if (key == VK_UP && historyPosition > 0) --historyPosition;
        if (key == VK_DOWN && historyPosition < history.size()) ++historyPosition;
        set_text(input, historyPosition < history.size() ? history[historyPosition] : L""); SendMessageW(input, EM_SETSEL, -1, -1); return 0;
    }
    return CallWindowProcW(originalInput, input, message, key, data);
}
void poll_session() {
    for (const auto& item : read_desktop_updates()) {
        if (item.type == UpdateType::Frame) capture(item);
        else if (item.type == UpdateType::Log) append(item.server ? serverLog : clientLog, wide(item.text));
        else if (item.type == UpdateType::Ready) {
            if (item.server) serverReady = true;
            else { ready = connected = true; set_text(statusLabel, L"Connected / 127.0.0.1:54000"); }
        } else {
            append(item.server ? serverLog : clientLog, wide(item.text));
            if (item.server) serverReady = false;
            else { ready = connected = false; demoCommands.clear(); }
            set_text(statusLabel, L"Session ended. Use Reconnect.");
        }
    }
    if (ready && !demoCommands.empty()) { wstring text = demoCommands.front(); demoCommands.pop_front(); send_input(text); }
    if (animation >= 0 && animation + 1 < static_cast<int>(frames.size()) && GetTickCount64() - animationStart > 1400) { ++animation; animationStart = GetTickCount64(); }
    buttons(); RECT wire{sx(200), sy(540), sx(1000), sy(610)}; InvalidateRect(window, &wire, TRUE);
}

//only the moving bytes need custom drawing; everything else is a normal control
void draw_transfer(HDC dc) {
    //clear only the animation area, keeping its drawing away from the controls
    RECT wire{sx(200), sy(540), sx(1000), sy(610)};
    int saved = SaveDC(dc);
    IntersectClipRect(dc, wire.left, wire.top, wire.right, wire.bottom);
    FillRect(dc, &wire, backgroundBrush);
    SelectObject(dc, monoFont); SetBkMode(dc, TRANSPARENT);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90)); HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, sx(210), sy(584), nullptr); LineTo(dc, sx(990), sy(584));
    if (animation >= 0) {
        const Frame& frame = frames[animation]; bool fromClient = frame.data.outgoing != frame.data.server;
        double progress = min(1.0, double(GetTickCount64() - animationStart) / 1100);
        int x = 220 + int((fromClient ? progress : 1 - progress) * 540);
        SetTextColor(dc, fromClient ? clientColor : serverColor);
        wstring name = title(frame); TextOutW(dc, sx(x), sy(546), name.c_str(), static_cast<int>(name.size()));
        for (int i = 0; i < 8; ++i) {
            RECT cell{sx(x + i * 28), sy(572), sx(x + i * 28 + 26), sy(602)}; FillRect(dc, &cell, terminalBrush); FrameRect(dc, &cell, backgroundBrush);
            wstring byte = hex_bytes(&frame.data.header[i], 1); DrawTextW(dc, byte.c_str(), -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectObject(dc, old); DeleteObject(pen);
    RestoreDC(dc, saved);
}
void create_controls() {
    label(L"NetFlex - Computer Networks Project", 28, 24, 475);
    statusLabel = label(L"Starting...", 530, 24, 310); demoButton = button(L"Run sample", Demo, 850, 18, 160); button(L"Reconnect", Restart, 1024, 18, 148);
    label(L"Client: text, /ping, /quit, /help     Server: /status, /clear, /restart", 28, 76, 1100);
    label(L"Client / 127.0.0.1:54000", 28, 110, 550); label(L"Server / 0.0.0.0:54000", 612, 110, 550);
    DWORD logStyle = WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL;
    clientLog = control(L"RICHEDIT50W", L"", logStyle, ClientLog, 28, 140, 560, 280); serverLog = control(L"RICHEDIT50W", L"", logStyle, ServerLog, 612, 140, 560, 280);
    clientInput = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ClientInput, 48, 444, 430, 28);
    serverInput = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ServerInput, 632, 444, 430, 28);
    label(L">", 28, 444, 20); label(L">", 612, 444, 20);
    for (HWND input : {clientInput, serverInput}) { SendMessageW(input, EM_SETLIMITTEXT, 65536, 0); originalInput = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(input_proc))); }
    for (HWND log : {clientLog, serverLog}) SendMessageW(log, EM_SETLIMITTEXT, 140000, 0);
    sendButton = button(L"Send", Send, 496, 438, 92); button(L"Run", Run, 1080, 438, 92);
    label(L"Message transfer (animation slowed for the demo)", 28, 505, 650); counters = label(L"", 810, 505, 362);
    control(L"STATIC", L"Client", SS_CENTER | SS_SUNKEN, 0, 28, 565, 165, 32); control(L"STATIC", L"Server", SS_CENTER | SS_SUNKEN, 0, 1007, 565, 165, 32);
    label(L"Message history", 28, 630, 300); frameList = control(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, Frames, 28, 665, 325, 145);
    description = label(L"", 385, 630, 420); receivedLabel = label(L"", 805, 630, 220); replayButton = button(L"Replay frame", Replay, 1040, 625, 132);
    for (int i = 0; i < 8; ++i) headerCells[i] = control(L"STATIC", L"--", SS_CENTER | SS_SUNKEN, 0, 385 + i * 56, 674, 48, 30);
    label(L"NF marker", 385, 713, 112); label(L"ver", 497, 713, 56); label(L"type", 553, 713, 56); label(L"length / big-endian", 609, 713, 240);
    payloadLabel = label(L"", 385, 745, 780, 67); label(L"Up/Down: command history | Select a message to inspect | Payload preview: first 64 bytes", 28, 820, 1144);
    layout();
}
//Windows calls this function when a button is clicked or the window changes
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM key, LPARAM data) {
    switch (message) {
    case WM_CREATE:
        window = hwnd;
        create_controls();
        SetTimer(hwnd, 1, 40, nullptr);
        PostMessageW(hwnd, WM_APP, 0, 0);
        return 0;
    case WM_APP:
        restart();
        return 0;
    case WM_SIZE:
        layout();
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize = {1040, 770};
        return 0;
    case WM_TIMER:
        poll_session();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        draw_transfer(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HWND item = reinterpret_cast<HWND>(data);
        bool terminal = item == clientLog || item == serverLog || item == clientInput
            || item == serverInput || item == frameList;
        HDC dc = reinterpret_cast<HDC>(key);
        SetTextColor(dc, textColor);
        SetBkColor(dc, terminal ? terminalColor : background);
        return reinterpret_cast<LRESULT>(terminal ? terminalBrush : backgroundBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(key)) {
        case Send: submit(true); break;
        case Run: submit(false); break;
        case Restart: restart(); break;
        case Demo: send_input(L"/demo"); break;
        case Replay:
            animation = selected;
            animationStart = GetTickCount64();
            break;
        case Frames:
            if (HIWORD(key) == LBN_SELCHANGE) {
                selected = static_cast<int>(SendMessageW(frameList, LB_GETCURSEL, 0, 0));
                followLatest = false;
                inspect();
            }
            break;
        }
        return 0;
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        stop_desktop_session();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, key, data);
}

//start networking, create the window, then handle Windows events
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        Network network;
        HMODULE editor = LoadLibraryW(L"Msftedit.dll");
        if (!editor) throw runtime_error("Windows text control could not be loaded.");
        SetProcessDPIAware();
        backgroundBrush = CreateSolidBrush(background);
        terminalBrush = CreateSolidBrush(terminalColor);
        normalFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        monoFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");

        WNDCLASSW type{};
        type.lpfnWndProc = window_proc;
        type.hInstance = instance;
        type.lpszClassName = L"NetFlexSimple";
        type.hCursor = LoadCursor(nullptr, IDC_ARROW);
        type.hbrBackground = backgroundBrush;
        if (!RegisterClassW(&type)) return 1;

        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        window = CreateWindowW(type.lpszClassName, L"NetFlex - Client and Server",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + 12, work.top + 12,
            min(1216L, work.right - work.left - 24), min(879L, work.bottom - work.top - 24),
            nullptr, nullptr, instance, nullptr);
        if (!window) return 1;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        ShowWindow(window, show);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        DeleteObject(normalFont);
        DeleteObject(monoFont);
        DeleteObject(backgroundBrush);
        DeleteObject(terminalBrush);
        FreeLibrary(editor);
    } catch (const exception& error) {
        stop_desktop_session();
        MessageBoxW(nullptr, wide(error.what()).c_str(), L"NetFlex", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
