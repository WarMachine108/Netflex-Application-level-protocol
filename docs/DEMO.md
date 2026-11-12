# Demonstration and troubleshooting

## Short classroom demonstration

1. Double-click `NetFlex.exe`; wait for the connected status.
2. Type `Hello` in the client pane. Show the server receiving it and the client echo.
3. Select the first Text message in Message history.
4. Explain `4E 46` (NF), `01` (version), `01` (Text), and `00 00 00 05` (five bytes).
5. Point out the actual payload hex `48 65 6C 6C 6F`.
6. Send `/ping`. Show the Ping and Pong frames with zero-length payloads.
7. Press Enter on an empty input. Show that an empty Text message is valid.
8. Send another text line to demonstrate several messages on the same connection.
9. Enter `/status` in the server control box.
10. Send `/quit`; the window stays open for inspection after the workers end.
11. Click Reconnect, then Run sample to start a fresh demonstration.

The sample sends `Hello NetFlex!`, `/ping`, and `Every byte has a meaning.`. A fresh
sample produces six frames: three client requests and three server replies. Each
frame has a send and receive observation, so Sent and Received both show six.

Replay is a slowed visualization of actual message bytes. Do not present its animation
speed as measured network latency or its blocks as captured TCP segments.

## Questions to be ready for

**Did you create TCP?** No. Windows implements TCP. NetFlex defines an application-layer
message format and conversation rules above it.

**Why isn't one recv enough?** TCP can split or combine application writes. The receiver
reads a fixed header, validates its length, and loops until the full payload arrives.

**Why does the length come first?** It tells the receiver where the payload ends and
where the next header starts, without relying on a special character in the content.

**Why big-endian?** It is the explicitly agreed byte order. Both ends reconstruct the
same number regardless of their native integer representation.

**Why check before allocating?** The peer controls its length field. A header claiming
billions of bytes must be rejected before using that number to allocate storage.

**Are the terminals fake?** The panes display real worker logs. Two threads communicate
over actual TCP sockets, sharing the same protocol/network modules as the console apps.

**Why threads and queues?** Blocking socket operations run outside the window thread.
Queues and synchronization let the interface submit commands and collect results safely.

**Can it run across machines?** The protocol can travel over reachable TCP connections,
but this desktop version hardcodes localhost. A remote destination requires a code
change and rebuild; it is not a current UI option.

## Troubleshooting

| Symptom | What to check |
|---|---|
| `g++` is not recognized | Install/configure MinGW-w64 and put its `bin` directory in PATH; reopen PowerShell |
| Thread-related compiler/link errors | Use a MinGW-w64 toolchain with C++17 and working thread support |
| Cannot write `NetFlex.exe` during build | Close the running application, then rebuild |
| Server cannot listen / Winsock 10048 | Another process may already occupy port 54000; close the other demo instance |
| Client is offline after `/quit` | Expected; click Reconnect |
| Send is unavailable | Wait for connection or the current reply; check server output if it stays unavailable |
| Message is too long | The limit is 65,536 encoded bytes, not 65,536 Unicode characters |
| Payload preview contains dots | The printable inspector substitutes non-ASCII/control bytes; the wire payload is unchanged |
| Console helpers appear as echoed text | `/help`, `/clear`, `/demo` belong to the desktop UI, not the standalone console client |

## Scope of verification

Local development checks exercised automatic startup, Text echo, Ping/Pong, Goodbye,
empty and UTF-8 payloads, oversized input retention, reconnect, command history,
frame selection/replay, resize handling, and worker shutdown. The repository does not
include an automated test suite. Physical two-machine communication and production
security are not claimed as verified features.
