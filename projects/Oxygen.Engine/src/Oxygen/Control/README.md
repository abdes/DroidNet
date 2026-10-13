# Oxygen Control

`Oxygen.Control` runs commands for every caller and lets programs drive a
running host application. It holds the command dispatcher and its jobs, the
event log, and the JSON-RPC control server. The design is
[commands and remote control](../../../design/runtime-core/commands-and-remote-control.md);
commands themselves are [`Oxygen.Commands`](../Commands/README.md).

The host composes it: the dispatcher, the event log and the optional server
are owned by the host and activated in its nursery, so jobs outlive module
teardown and stop with the host. The engine only owns the command registry.

## Dispatcher

`CommandDispatcher` runs every invocation, from any source, as a job.

- `Post(invocation, context)` and `PostLine(line, context)` queue work;
  `Run(invocation, context)` queues and awaits the result (abandoning the
  await cancels the job); `Cancel(job)` cancels a queued or running job.
- Work starts at the next frame start, on the engine thread.
  `CommandDispatchModule`, an engine module at the highest frame-start
  priority, ticks it.
- Jobs start in arrival order, and each runs until it first waits before the
  next starts, so effects apply in the order they were sent.
- An asynchronous handler awaits `NextFrame()` (or anything else) and keeps
  running in the dispatcher's nursery. A job from a console line records its
  result in the console's execution records.
- Each job is logged when it starts and finishes (verbosity 1); a job still
  running after 600 frames is reported once as a warning.

Local front ends (toolbar, key bindings, console, palette, window close) call
`PostLine` in process. Game input does not go through the dispatcher.

It registers the commands whose replies need frames:

| Command              | Reply                                                                               |
| -------------------- | ----------------------------------------------------------------------------------- |
| `ngin.pause`         | At once.                                                                            |
| `ngin.resume`        | At once; cancels pending steps.                                                     |
| `ngin.step [frames]` | Once the frames have run; `data` has `frames` and `ran`; fewer ran is `kCancelled`. |
| `ngin.session.end`   | Once the session has ended.                                                         |
| `ngin.quit`          | At once; the engine stops.                                                          |
| `cmd.jobs`           | Running jobs: id, command, source, client, start frame.                             |
| `cmd.cancel <job>`   | At once; `kNotFound` when no such job is queued or running.                         |

## Event log

`EventLog` numbers events across all topics and keeps the last 4,096 in a
ring buffer, so a client can catch up after a gap. `Publish(topic, data)` is
thread-safe. Publishers can ask whether a topic has interest
(`HasInterest`) to produce costly events only while someone listens.

Topics this module publishes:

| Topic     | Data                                                                                                                                                                  |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `command` | `job`, `commands` (`command`, `args` each), `source`, `status`, `start_frame`; `client` for remote jobs, `line` for console lines, `error` when the job failed.       |
| `log`     | `severity` (`info`, `warning`, `error`, `fatal`), `channel` (the source file stem, or `script` for `oxygen.log`), `text`. Captured from loguru while the server runs. |

Other publishers define their own topics; the player's are in
`oxygen.player-events.schema.json`.

## Control server

`ControlServer` speaks JSON-RPC 2.0, one JSON object per line, over loopback
TCP. Its coroutines run on the platform's `io_context`, which the host loop
polls on the engine thread; it has no thread of its own.

- On start it writes `<pid>.json` (protocol, pid, application, port, token)
  to `%LOCALAPPDATA%\Oxygen\control` (`$XDG_RUNTIME_DIR/Oxygen/control`
  elsewhere) and removes it on exit.
- The first message must be `rpc.hello` with the token; a client may claim
  ownership there. With `exit_when_owner_leaves`, losing the owner, or no
  owner within 10 seconds, posts `ngin.quit`.
- `rpc.subscribe`, `rpc.unsubscribe` and `rpc.events` manage event delivery
  and catch-up; `rpc.cancel` cancels an earlier request of the connection.
- Every other method is a command name; the reply comes when the command
  finishes. Limits: 1 MiB per line, 8 connections.

Hosts enable it with `HostConfig::control`; the player also from its launch
file or `--control [port]`.

```json
{"jsonrpc":"2.0","id":1,"method":"rpc.hello","params":{"token":"…","client":"agent","owner":true}}
{"jsonrpc":"2.0","id":2,"method":"ngin.step","params":{"frames":3}}
{"jsonrpc":"2.0","id":3,"method":"rpc.subscribe","params":{"topics":["lifecycle","log"]}}
```

## Testing

`Test/` covers the event log, the dispatcher against a test engine (frame
start, ordering, stepping, cancellation) and the server with an in-process
client (handshake, requests, errors, events and catch-up).
