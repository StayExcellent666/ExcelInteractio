# ExcelProtocol integration contract

Excel Interactio is a separate paired device using the existing `/api/fx/pair` and `/api/fx/ws` flow, protocol `excelprotocol-fx`, version `1`. It stores its independent device credential in Windows Credential Manager and advertises only `interactio.chat.v1` in its hello capabilities. It must never replace or reuse an FX device credential.

## ExcelProtocol to plugin: chat command events

For each actual Twitch chat message for the paired device's guild/broadcaster, send the standard FX request envelope with a unique `request_id`, short-lived `expires_at`, and this command:

```json
{"type":"interactio.chat_message","message":"!example","event_id":"twitch-chat:unique-message-id"}
```

Messages are text, at most 500 characters. Route only to authenticated, connected devices in the originating guild that advertise `interactio.chat.v1`; don't forward bot-originated messages. Deduplicate Twitch event IDs and don't replay stale chat. The plugin responds through the existing FX response envelope with `{"ok":true,"status":"ok"}` for accepted messages, or `{"ok":false,"status":"error","error":"..."}` for rejected/expired ones. Extend the existing validation and capability mapping narrowly; do not weaken FX permissions, rate limits, pairing, or command validation. Activate the relevant Twitch EventSub subscription for connected Interactio devices even when no Raid Battle is active.

## Plugin to ExcelProtocol: request bot chat

The plugin sends this JSON on the authenticated FX WebSocket:

```json
{"protocol":"excelprotocol-fx","protocol_version":1,"type":"interactio.chat_send","request_id":"unique-uuid","message":"Hey people"}
```

Validate the exact envelope, capability, authenticated current session, guild-to-broadcaster/bot mapping, text length and content. Enforce sensible guild/device rate limits and request-ID deduplication. The paired guild's bot alone must send the message; never accept a caller-supplied channel or credentials. Respond with:

```json
{"protocol":"excelprotocol-fx","protocol_version":1,"type":"interactio.chat_send_result","request_id":"same-uuid","ok":true,"error":""}
```

On failure set `ok` to false and supply a bounded, non-secret error. Don't replay requests after disconnect. Existing FX clients must continue to work unchanged.

## Timer semantics

Timers run in the OBS plugin, independently of the server and whether the stream is live. When a timer fires, it sends a single request if paired and connected; a disconnected request is skipped, not queued. A failure is recorded in the OBS log. No server-side timer should duplicate the schedule.

## Verification

Test cross-guild isolation, credential revocation, pairing, capability gates, exact schemas, duplicate events and requests, bot echo suppression, limits, rate limiting, disconnect/reconnect, no replay, and unchanged FX behavior. Add or update the workflow finder under the server repository's AGENTS.md instructions. Verify end-to-end with the plugin only after the backend changes are deployed to the intended server.
