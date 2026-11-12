# Device WebSocket contract (ADR-0056)

RustFox serves this socket on the same bind and port as the portal when
`[device_ws] enabled = true` (default false). Firmware in this repository is
the client. The normative server behavior is `src/device_ws.rs` in
chinkan/RustFox. This page is the client-side copy of that contract.

## Transport

| Item | Value |
| --- | --- |
| Method and path | `GET /api/device/v1/ws` |
| Frames | WebSocket **text** frames containing one JSON object |
| Binary frames | Rejected by the server with `error` / `bad_frame` (`json text frames only`). This client ignores them. |
| Partial token streaming | Out of scope |
| Auth wait | 15 seconds for a first-frame `auth` |
| Max `user_text` | 16000 Unicode scalars after trim |
| Server max frame | 256 KiB |
| Client reassembly cap | 8 KiB. Larger text frames are dropped. Parsed `text` kept for the log and the face is 1023 bytes. |

Use `ws://` on loopback, Tailscale, or a trusted LAN. Use `wss://` when TLS
terminates at a reverse proxy in front of RustFox. RustFox does not terminate
TLS. A bare public WebSocket is not recommended.

## Auth

Per-device token from Portal Settings → Devices. It is not the portal login
token. The plaintext is shown once at create or rotate. Revoked tokens fail.

Either of these is accepted:

1. Request header `Authorization: Bearer <token>`. A wrong or revoked token is
   **HTTP 401** and the upgrade does not complete. There is no JSON body on
   the socket.
2. First text frame `{"type":"auth","token":"<token>"}` when that header is
   absent. A wrong token gets an `error` frame and the server closes. Other
   frames before a successful `auth` get `error` / `unauthorized` and the
   socket stays open until the 15 second budget ends.

On success the server sends:

```json
{"type":"device_ack","status":"authenticated","device_id":"<registry id>"}
```

`room` is present when the device record has one.

This firmware's default is the header. `RustFox ESP32-C6 → Device token
transport` can switch to the first frame. The token is not printed.

## Frames

| Direction | `type` | Fields | When |
| --- | --- | --- | --- |
| Client → server | `auth` | `token` | First frame only, and only if the Bearer header was not sent |
| Client → server | `user_text` | `text` | Required to start a turn. Trimmed, non-empty, at most 16000 scalars |
| Client → server | `ping` | none | Optional keepalive. This client sends one every 30 s after auth |
| Client → server | `device_hello` | `device_id`, optional `room`, optional `fw_version` | Optional. Sent after `authenticated` |
| Server → client | `assistant_text` | `text` | One frame per reply string. A turn may send several |
| Server → client | `assistant_audio` | `id`, `url`, `sample_rate`, `channels`, `encoding` | PCM for this device is ready. Play only 16000 Hz, 1 channel, `s16le` |
| Server → client | `device_ack` | `status`, `device_id`, optional `room` | See status values below |
| Server → client | `pong` | none | Reply to a JSON `ping` |
| Server → client | `error` | `code`, `message` | Rejected frame, auth failure, or a failed turn |

`device_ack.status`:

| Status | Meaning |
| --- | --- |
| `authenticated` | Token accepted |
| `hello` | `device_hello` stored for later turns |
| `queued` | The interactive lane is busy; the text was queued |

Hello metadata is sanitized by the server: trim, drop control characters and
`[` `]`, keep at most 64 scalars. This client applies the same rules before
sending so the ack matches.

A `user_text` is the owner's normal interactive turn. The server may prefix
the stored line with `[device id=… room=… fw=…]`. The board does not send
that prefix.

## Errors

| `code` | Typical `message` | Client reaction in v0 |
| --- | --- | --- |
| `unauthorized` | `invalid device token`, `authentication required` | Worried face, stop reconnecting |
| `bad_frame` | `user_text requires text`, `user_text is too long`, `unknown frame type`, `expected a JSON text frame`, `json text frames only`, `frame is too large` | Worried face, stay connected |
| `queue_full` | `too many messages are waiting` | Worried face |
| `agent` | `the assistant turn failed` | Sad face |
| `internal` | `device token store unavailable` | Worried face |

Protocol-level WebSocket ping/pong (opcode 0x9 / 0xA) is separate. The server
answers those inside the socket library. They do not replace JSON `ping`.

## Audio

Press-and-hold on the glass records 16 kHz mono PCM into the `audio` flash
partition. Release uploads one WAV:

| Item | Value |
| --- | --- |
| Upload | `POST /api/device/v1/audio` |
| Body | `Content-Type: audio/wav`, 16 kHz, mono, s16le |
| Auth | `Authorization: Bearer` with the same device token as the socket |
| Download | `GET /api/device/v1/audio/{id}` returns raw PCM (`application/octet-stream`) |
| Playable frame | `sample_rate` 16000, `channels` 1, `encoding` `s16le`, `url` a path on this host |

```json
{"type":"assistant_audio","id":"clip-1","url":"/api/device/v1/audio/clip-1","sample_rate":16000,"channels":1,"encoding":"s16le"}
```

The origin is the Device WebSocket URI with `ws://` rewritten to `http://` and
`wss://` rewritten to `https://`. The host transcribes the WAV and starts the
turn from that transcript. Holding again while PCM is playing stops playback
and starts a new recording. A drop of Wi-Fi or the socket during the hold
discards that clip and shows the worried face. `unauthorized` still stops
reconnecting.

## v0 client sequence

1. Connect.
2. Header auth: wait for `device_ack` / `authenticated`. First-frame auth: send `auth`, then wait for the same ack.
3. Send `device_hello` (`device_id`, optional `room`, `fw_version` `0.1.0`).
4. On `device_ack` / `hello`, send one canned `user_text` if that option is on and no manual line is waiting.
5. BOOT (GPIO9) or a line typed into the serial monitor submits another `user_text`.
6. Show `assistant_text` on the log and map a few substrings onto the fox face.
7. Press-and-hold uploads `POST /api/device/v1/audio`. On `assistant_audio`, `GET` the path and play the PCM.
