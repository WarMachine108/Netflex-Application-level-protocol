# NetFlex version 1: protocol specification

NetFlex is an application-layer message protocol carried by TCP. It uses a fixed
eight-byte header followed by exactly the declared number of payload bytes. No
newline, separator, terminating null byte, or TCP segment boundary marks a frame.

## Wire format

All offsets are zero-based. Each byte is eight bits.

| Offset | Width | Name | Value |
|---|---:|---|---|
| 0 | 2 bytes | Marker | `4E 46`, ASCII `NF` |
| 2 | 1 byte | Version | `01` |
| 3 | 1 byte | Type | An integer from 1 through 5 |
| 4 | 4 bytes | Payload length | Unsigned 32-bit integer, most significant byte first |
| 8 | Declared length | Payload | Raw content bytes |

The length must be between **0 and 65,536 inclusive**. This implementation deliberately
accepts less than the four-byte integer's full representable range. Length counts
bytes, not Unicode characters. There is no text-encoding field or UTF-8 validation.

Fields are encoded individually. The program does not send a C++ `Message` struct's
memory layout, which would contain implementation-specific representation and padding.

## Types

| Value | Type | Valid payload | Conversation rule |
|---|---|---|---|
| `01` | Text | 0–65,536 bytes | Client sends; server replies with identical Text bytes |
| `02` | Ping | Empty | Client sends; server replies with Pong |
| `03` | Pong | Empty | Server reply to Ping |
| `04` | Goodbye | Empty | Client requests closure; server acknowledges with Goodbye |
| `05` | Error | 0–65,536 bytes | Server explains a recognized but disallowed client request |

The client sends one request and waits for one reply before sending the next. Pong
and Error are structurally valid types, but the server does not accept them as
normal client requests; it replies with Error. Numeric types outside 1–5 are invalid.

## Examples

Text `Hello`, 13 bytes total:

```text
4E 46 01 01 00 00 00 05 48 65 6C 6C 6F
```

Empty Text, eight bytes total:

```text
4E 46 01 01 00 00 00 00
```

Ping and its Pong reply, each eight bytes:

```text
Client -> Server: 4E 46 01 02 00 00 00 00
Server -> Client: 4E 46 01 03 00 00 00 00
```

A 300-byte payload has length bytes `00 00 01 2C` because 300 is `1 * 256 + 44`.
The largest accepted payload, 65,536 bytes, has length bytes `00 01 00 00`.

## Framing and validation

`receive_message` performs these steps:

1. Read exactly eight header bytes using `receive_exact`.
2. If the peer closes before any header byte arrives, return false: there is no next message.
3. If it closes partway through a header, report an incomplete read.
4. Validate marker, version, and type, in that order.
5. Reconstruct the big-endian length and reject values above the payload limit.
6. Require Ping, Pong, and Goodbye to have zero-length payloads.
7. Allocate payload storage only after those checks.
8. Read exactly the payload length; report closure before completion as an error.
9. Return the complete Message and repeat at the next header when requested.

Zero-length reads succeed immediately. A frame may arrive across several receives,
and several frames may be queued together. Both cases are normal TCP behavior.

`send_message` encodes the header, runs the same header validation on the outgoing
fields, then calls `send_all` for the header and payload. A successful send means
the local socket accepted the bytes, not that the peer application processed them.
The reply supplies application-level evidence of processing.

## Error and closure behavior

Invalid markers, unsupported versions/types, invalid control lengths, oversized
lengths, socket failures, and incomplete frames produce local errors. They end the
current worker conversation or console program. The implementation does not promise
an Error frame in response to malformed bytes and does not search the stream for a
new marker after rejecting a header.

Recognized message types used in the wrong request direction produce an Error reply
without immediately ending the server's message loop.

On normal `/quit`, the client sends Goodbye, receives the server's Goodbye, and then
ends its worker loop. Socket objects close their handles when they leave scope. The
desktop window stays open so the transcript can be inspected. Reconnect stops/joins
the previous workers and starts a new listener and client. Closing the desktop app
signals shutdown, wakes waiting workers, and joins them before exiting.

## What the visualization measures

Desktop trace hooks execute after an entire message send or receive completes.
Each direction has its own sequence numbers. The UI groups the sender's and
receiver's observations using direction and sequence, so each frame appears once.

Header bytes are real encoded/received bytes. The animated travel time is illustrative
and deliberately slowed; it is not a measurement of packet timing, individual TCP
segments, acknowledgements, retransmissions, or network latency. Byte counters include
only NetFlex headers and payloads sent by the two endpoints.
