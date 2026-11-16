# services/messenger – Dev's Telegram chat (D22)

Delivers notices and questions from any service to Dev's private chat with Buddy's bot and hands answers back on
the bus. User-facing behaviour, pairing, troubleshooting: `docs/guides/01-setup-summary.md` → 1.16. Payloads:
`shared/topics.json` (`messenger_cmd`, `messenger_event`, `messenger_state`).

| File | What |
|---|---|
| `command.*` | cmd JSON → `Notify` / `Ask` / `Cancel` (`parse_command` throws, never quotes the payload), `clip()` UTF-8-safe, size limits |
| `chat.*` | Everything about the conversation, no I/O, clock passed in. In: `command()`, `incoming()` (what Dev did), `tick()`. Out: `outbox()` (`Send` / `Edit` / `AnswerCallback`, front first; main reports `sent(tag, message_id)` / `failed(tag, reason)`), `take_events()`, `take_dirty()` + `dump()` / `load()`. Matches answers (button data `q<n>:<i>`, reply-to, single open question, "which question?" prompt `p<pick>:<n>`), `/status` (plain "what's the status" too, `is_status_question`), `/open`, keyed notices, expiry, dedupe by `last_message_id` |
| `fleet.*` | `buddy/system*/state` (D23) + online flags → one short line per host for `/status` (CPU, temp, RAM, disk, throttle flags; offline hosts marked) |
| `watch.*` | `buddy/+/status` + steady clock → notices with key `watch:<svc>` (offline, back, crash loop); `summary()` for `/status` |
| `telegram.*` | Bot API on `Http`: `send`, `edit`, `answer_callback`, `set_commands`, `confirm`, `updates_url()` + `parse_updates()` for the long poll; `classify()` (what an `ApiError` means for the message), credentials file, `Update::from()` / `carries_code()` |
| `store.*` | JSON file: tmp + fsync + rename, mode 600 |
| `main.cpp` | Poll thread (`CurlHttp::stream` on getUpdates, own connection) → `BlockingQueue` ← MQTT callbacks; main thread feeds `Chat`/`Watch`/`Fleet`, delivers the outbox (≤ 1 message/s, backoff 1 → 60 s), saves, publishes events and the retained state. `--pair`, `--notify`, `--ask` |

## How a question travels

`ask` → `Chat` queues a `Send` → delivered → `sent()` → event `asked` (only now: the asker knows Dev has it) →
Dev taps or writes → poll thread → `incoming()` → event `answered`, `Edit` of the question's message. Order in the
loop: handle, **save, then publish**, then `Ack` the poll thread, which only then confirms the batch to Telegram
(next `offset`). A crash anywhere gets the update again; `Chat` drops what it already handled.

## Gotchas

- **The token is in every URL.** `CurlHttp` puts URLs into its errors: everything that can carry one goes through
  `Telegram::redact()` before it is logged or thrown on. Tests check it.
- **No message texts in the journal**, also not in errors (`parse_command` reports the reason and a byte count).
  Telegram's own error descriptions are fine.
- **One listener per bot:** a second getUpdates → 409 for the first. `--ask` and `--pair` need the unit stopped
  (unpaired, the unit doesn't poll); `--notify` doesn't.
- The poll's `offset` isn't saved: after a restart Telegram resends the last batch, `Chat` ignores it
  (`last_message_id`; a repeated button press only heals the buttons).
- Link state = trouble sending if any (`send_link`), else the poll thread's view. `blocked` clears with the next
  message that goes out; `unreachable` from sending clears when the outbox is empty.
- A failed question is forgotten (the asker may ask again under the same id); answered and expired ones are kept
  24 h so a repeated `ask` gets its result.
- `Watch` starts empty: services still offline are reported again after messenger restarts.
- Not unit-tested: `main.cpp`. Checked 2026-11-15 against a throwaway fake Bot API (`kApi` pointed at localhost in
  a local build): pairing, ask/answer across a restart, foreign account, 403, 409, 429, API down and back.
- GCC: designated initializers on `Send`, `Event`, … need every field (`-Wmissing-field-initializers`).
