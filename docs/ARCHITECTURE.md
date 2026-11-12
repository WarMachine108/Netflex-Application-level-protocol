# Architecture and code guide

## Two ways to run NetFlex

The **desktop app** builds `desktop.cpp`, `desktop_session.cpp`, `network.cpp`, and
`protocol.cpp`. It runs a server worker and client worker in the same process, using
real TCP sockets between them. Its terminal panes display worker log updates.

The **console programs** build `client.cpp` or `server.cpp` with the same network and
protocol modules. They are independent entry points and are not hidden child processes
of the current desktop app. The documented console build does not enable the
`NETFLEX_DESKTOP` macro or depend on the desktop tracing hooks.

## Module responsibilities

| Module | Responsibility |
|---|---|
| `network.h/.cpp` | Windows socket setup, cleanup, socket ownership, and byte-transfer loops |
| `protocol.h/.cpp` | Message definitions, eight-byte header encoding/validation, complete frames |
| `desktop_session.h/.cpp` | Worker lifecycle, TCP conversation, command/update queues, trace observations |
| `desktop.cpp` | Native Windows controls, keyboard input, local commands, history, inspection, animation |
| `client.cpp` | Console input, request creation, connection, and reply checking |
| `server.cpp` | Console listener, acceptance, processing, and replies |

## Trace one text message

1. The UI reads a line from the client input box and converts Windows Unicode text to UTF-8.
2. It checks the byte length, displays the typed line, and queues the command.
3. The client worker wakes and creates `Message{MessageType::Text, input}`.
4. `send_message` encodes/validates a header and sends header plus payload completely.
5. A desktop trace update records the completed send.
6. The server's `receive_message` reads/validates the header, then reads the full payload.
7. The server records the receive, logs the text, and sends the same Message back.
8. The client reads the complete reply and checks that its type matches the request.
9. The UI collects log/frame updates, updates the inspector, and enables the next request.

Transport and UI updates are separate. A frame can be received before the UI processes
its sender's trace update; direction and sequence numbers merge the two observations.

## Worker threads and shared state

`serverThread` binds/listens on port 54000. `clientThread` waits for the listener to be
ready before connecting. Each worker performs its own blocking network operations.

`sessionMutex` protects command/update queues, startup flags, and shared active socket
handles. `lock_guard` holds the mutex for a short scope. `unique_lock` can temporarily
release it while a condition variable waits. `changed` wakes workers when a command,
startup result, or stop request arrives; it avoids repeatedly checking an empty queue.

`stopping` is atomic because multiple threads check it during their loops. The server
checks its listening socket with a 100 ms select timeout, allowing a waiting listener
to stop. `stop_desktop_session` signals stopping, shuts down active connection sockets
to wake receives, notifies waiting workers, and joins both threads.

`ActiveSocket` registers a worker's socket while that socket is alive. Its destructor
clears the shared handle before the owning `Socket` destructor closes it. This keeps
shutdown from acting on a handle that a worker has already released.

`serverSide` and the trace counters are thread-local: each worker has its own values.
`read_desktop_updates` swaps the collected updates into a local vector while holding
the mutex; the window processes them after releasing that lock.

## Function reference

### Networking and protocol

| Function/type | Purpose |
|---|---|
| `Network` | Calls WSAStartup when constructed and WSACleanup when destroyed |
| `Socket` | Owns one checked handle and calls closesocket on destruction |
| `Socket::get` | Returns the handle without transferring ownership |
| `send_all` | Tracks `sent` and keeps sending the remaining bytes |
| `receive_exact` | Tracks `received`; returns false on EOF before any bytes, throws on truncation |
| `MessageType` | Named numeric choices for the five message types |
| `Message` | A type plus the actual payload string |
| `MessageHeader` | A checked type and expected payload length |
| `encode_header` | Builds the explicit eight-byte header |
| `decode_header` | Checks fields and reconstructs the payload length |
| `send_message` | Validates and sends one header/payload pair |
| `receive_message` | Builds one complete, validated Message from the TCP stream |

### Desktop session

| Function/type | Purpose |
|---|---|
| `DesktopUpdate` | Carries a log, readiness, frame, or completion update to the UI |
| `server_loop` / `client_loop` | Execute the two sides of the conversation |
| `start_desktop_session` | Stops the old session, clears shared state, starts fresh threads |
| `stop_desktop_session` | Requests shutdown and joins workers |
| `send_desktop_command` | Adds a command and wakes the client worker |
| `read_desktop_updates` | Retrieves pending worker updates safely |
| `desktop_trace` | Records completed network-message operations with a 64-byte preview |

### Window and controls

| Function | Purpose |
|---|---|
| `wWinMain` | Starts Winsock, creates the window, and runs the Windows message loop |
| `window_proc` | Handles button clicks, resize, repaint, timers, and window closure |
| `create_controls`, `control`, `layout` | Create/position normal Windows controls |
| `wide`, `utf8` | Convert between Windows text and UTF-8 |
| `append` | Append terminal output, trimming very old display text when needed |
| `submit`, `send_input` | Handle local commands and queue network requests |
| `input_proc` | Add Enter submission and Up/Down command recall to input controls |
| `poll_session` | Process worker updates on the UI thread every 40 ms |
| `capture` | Merge send/receive observations and maintain the bounded frame list |
| `inspect` | Show the selected frame's header and payload preview |
| `draw_transfer` | Draw the slowed header-byte animation |
| `restart` | Clear the current desktop session display and restart the workers |

## Important variables

| Name | Meaning |
|---|---|
| `handle` | Windows identifier for a socket owned by a Socket object |
| `address` | IPv4 address/port structure passed to bind or connect |
| `listener`, `peer` | Listening socket and connected communication socket |
| `input`, `message`, `reply` | Typed command, outgoing/received message, and response |
| `header`, `decoded`, `payload` | Wire header bytes, checked metadata, and content |
| `sent`, `received`, `count` in network.cpp | Total progress and progress from one socket call |
| `commands`, `updates` | Shared worker command queue and UI update list |
| `ready`, `connected`, `serverReady` | UI state used for status and button availability |
| `frames`, `selected`, `animation` | Captured history and the inspected/animated indices |
| `byteCount` | NetFlex bytes observed sent in both directions, including headers |

Resource-owning objects cannot be copied. Their destructors provide automatic cleanup
(RAII). Header byte shifts explicitly implement network byte order; they do not change
TCP itself.
