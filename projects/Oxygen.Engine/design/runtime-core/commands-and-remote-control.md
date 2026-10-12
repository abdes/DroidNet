# Commands and remote control

Status: **approved 2026-10-13; decisions RC-D1 to RC-D6 taken
2026-10-13. New ground: this design is expected to change as R1 is built,
and changes are recorded here as they are made**. Delivery step R1 of
[runtime host §9](runtime-host.md#9-delivery-order-and-exit-gates), before
E2. Decisions are in [§10](#10-decisions).

Summary:

- **Every interaction is a command.** A toolbar button, a menu item, a key
  binding, the command palette, a console line, closing the window and a
  remote request all run a registered command through one dispatcher. No UI
  code changes engine, host or scene state any other way
  ([§2](#2-the-rule)).
- Commands get their own module, `Oxygen.Commands`; the console becomes a
  text front end over it and keeps console variables. A command declares
  typed parameters, a label, availability and a
  structured result. The same declaration gives the console its grammar and
  help and gives remote callers a JSON Schema. A command may take several
  frames; it then runs as a job that can be awaited and cancelled
  ([§3](#3-command-model)).
- `Oxygen.Host` runs an opt-in control server: JSON-RPC 2.0, one JSON
  object per line, over loopback TCP, on the platform's `io_context` on the
  engine thread. Every command is a JSON-RPC method of the same name
  ([§4](#4-control-server), [§5](#5-protocol)).
- Events (lifecycle, log, script diagnostics, executed commands, GPU work)
  are numbered notifications kept in a ring buffer, so a client can catch up
  after a gap ([§6](#6-events)).
- The player's named pipe, its request schema and its reader thread are
  removed; the editor and `play_scene` use the control server
  ([§7](#7-oxygenplayer), [§8](#8-consumers)).
- An MCP server for agents (R2) and frame capture, scene queries and input
  injection (R3) build on this and are not part of R1
  ([§11](#11-not-in-scope)).

## 1. Findings

| Finding                                                                                                                                                                                                                                                                              | Where                                                                                                |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------- |
| The viewport toolbar already acts only through console lines: view mode, show flags, stats and Play/Pause/Step/Stop each run a command or set a console variable.                                                                                                                    | `Host/ViewportToolbar.cpp:117-128`, `:318-336`                                                       |
| Some interactions bypass commands: the toolbar's console button and the backtick and Ctrl+Shift+P hotkeys change `ConsoleUiState` directly; closing the window calls `engine->Stop()`.                                                                                               | `Host/ViewportToolbar.cpp:455-464`; `ImGui/Console/ConsoleHost.cpp:21-32`; `Host/RunHost.cpp:99-104` |
| The player's pipe requests call the engine directly. `play` remounts the content but the toolbar's Play (`ngin.session.begin`) does not, so one interaction has two behaviours. `stop` and `stats` have no command at all.                                                           | `Player/PlayerModule.cpp:342-367`, `:487-529`                                                        |
| Commands are text in, text out and synchronous. Handlers receive `std::vector<std::string>` and parse it themselves; nothing describes their parameters, results are text, and a command whose effect takes frames (`ngin.step 3`, a scene load) returns before the effect is known. | `Console/Command.h`; `Engine/AsyncEngine.cpp:982-1004`                                               |
| The console already models command sources (`kRemote`, `kAutomation`, …), per-source policy and an audit hook. Remote execution is opt-in per command (`kRemoteAllowed`), and no command sets it, so nothing is remotely invocable today.                                            | `Console/Registry.h`; `Console/Registry.cpp:1870-1900`                                               |
| UI availability is ad hoc: the toolbar decides that sessions exist by checking whether `ngin.session.begin` is registered.                                                                                                                                                           | `Host/ViewportToolbar.cpp:306-315`                                                                   |
| The platform owns an `asio::io_context`, polled by the host's event loop on the engine thread; OxCo already adapts Asio operations into awaitables with cancellation.                                                                                                                | `Platform/Platform.h:99`; `Platform/AsyncOps.cpp:84`; `Host/RunHost.cpp:35-44`; `OxCo/asio.h`        |
| About 35 commands are registered: engine, content, graphics, scripting, the player, and DemoShell's `pp.*` and LightBench's `lightbench.*`.                                                                                                                                          | `RegisterCommand` call sites                                                                         |

## 2. The rule

An **interaction** is anything a user or a program does that changes the
state of the engine, the scene, the host or its debug UI: play state, view
mode, show flags, panel visibility, console variables, content mounts,
quitting. Every interaction runs a command through the engine's command
dispatcher ([§3.4](#34-dispatch-and-jobs)), whatever started it:

| Origin                      | How it runs a command                                                                  |
| --------------------------- | -------------------------------------------------------------------------------------- |
| Toolbar button, menu item   | The widget holds an invocation (command name and arguments) and posts it when clicked. |
| Key binding                 | The binding table maps a chord to an invocation ([§3.6](#36-key-bindings)).            |
| Console line, palette entry | The line is parsed into an invocation by the command's own parameters.                 |
| Window close button         | Posts `ngin.quit`.                                                                     |
| Launch file, `-c`, `exec`   | Console variable sets and command lines, as today, through the same dispatcher.        |
| Remote client               | A JSON-RPC request whose method is the command name ([§5.3](#53-commands-as-methods)). |

Not interactions, and not commands: presentation inside a widget (typing in
the console's input field, moving the palette cursor, hovering, scrolling,
opening a dropdown), and game input routed to the scene's input mapping
contexts. Injecting game input from a program is itself a command (R3).

Consequences:

- Anything a person can do in the window, a remote client can do, with the
  same validation and the same result.
- Every executed command is an event with its source
  ([§6](#6-events)), so a remote client sees what a person clicked, and a
  person's session can be replayed from its command events.
- A widget never decides availability itself: it reads the command's
  availability and shows the reason when disabled.

The rule binds every host-based application. E2 converts DemoShell's
panels: a panel control that changes settings or state posts a command or
sets a console variable; panel layout and widget presentation stay local.

## 3. Command model

Commands move out of the console into a new module, `Oxygen.Commands`. A
command is broader than a console line: a toolbar, a key, a game's own menu
or a remote client runs it with typed arguments and may await it across
frames. The console is one text front end among those callers, and it also
ships in games, where it must stay small and expose only what a shipped
build allows.

| Module            | Owns                                                                                                                                                          | Depends on                |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| `Oxygen.Commands` | Command definitions, parameters and their schemas, arguments, results, availability, sources and per-source policy, the audit hook, the registry and `Run`    | Base, OxCo, nlohmann json |
| `Oxygen.Console`  | Console variables (registry, latching, persistence, startup plan), the text front end (parser, history, completion), `help`, `find`, `list`, `exec`, `cvar.*` | Base, Config, Commands    |
| `Oxygen.Engine`   | The registry and console instances, dispatch at frame start and jobs ([§3.4](#34-dispatch-and-jobs))                                                          | both                      |

The console turns a text line into an invocation and runs it through the
command registry; it no longer stores commands. Console variables stay in the
console, and the console registers the `cvar.*` commands that read and set
them. The free-form `std::vector<std::string>` handler is removed, and every
registered command is migrated in R1 (no second command path).

### 3.1 Definition

```cpp
struct CommandDefinition {
  std::string name;            // "ngin.step"; also the JSON-RPC method
  std::string label;           // "Step"; menus, palette, tooltips
  std::string help;            // one or two sentences
  std::vector<CommandParam> params;
  CommandFlags flags { CommandFlags::kNone };
  //! Optional; absent means always available.
  std::function<auto(const CommandContext&)->CommandAvailability>
    availability;
  //! Exactly one of the two.
  CommandHandler handler;           // completes immediately
  AsyncCommandHandler async_handler; // may span frames (OxCo)
  //! Optional JSON Schema of `CommandResult::data`.
  std::optional<nlohmann::json> result_schema;
};

struct CommandAvailability {
  bool enabled { true };
  std::string reason; // why not, for tooltips and remote errors
};

using CommandHandler = std::function<auto(
  const CommandArgs&, const CommandContext&)->CommandResult>;
using AsyncCommandHandler = std::function<auto(
  CommandArgs, CommandContext)->co::Co<CommandResult>>;
```

The types live in namespace `oxygen::commands`. `CommandContext` and
`CommandSource` move there from the console; the context gains the `JobId`
of the running invocation and, for remote sources, the client name used in
audit events.

### 3.2 Parameters

```cpp
struct CommandParam {
  std::string name;   // "frames"
  CommandParamType type; // kBool, kInt, kDouble, kString, kEnum, kPath
  std::string help;
  bool required { false };
  std::optional<CVarValue> default_value;
  std::optional<double> min_value, max_value;
  std::vector<std::string> allowed_values; // kEnum
  bool rest_of_line { false }; // last string param only: `find`, `exec`
};
```

- **One parameter list, two syntaxes.** Console text is positional in
  declaration order, with `name=value` allowed for any parameter; JSON
  callers pass an object. Both produce the same `CommandArgs`, typed,
  defaulted and range-checked before the handler runs. Handlers no longer
  parse text.
- **Schema.** The registry builds each command's JSON Schema from its
  parameters (`additionalProperties: false`) for clients and tools. The
  parameter list is itself the validator: text and JSON are both converted
  through it, so their errors match and `Oxygen.Commands` needs no schema
  validator library.
- **Completion and help.** `help <name>` prints the label, parameters and
  defaults; completion offers `kEnum` values and parameter names.

### 3.3 Results

```cpp
struct CommandResult {
  ExecutionStatus status { ExecutionStatus::kOk };
  std::string output;   // human-readable, shown by the console
  std::string error;
  nlohmann::json data;  // structured, for programs; may be null
};
```

`ExecutionStatus` gains `kDisabled` (availability said no; `error` holds
the reason), `kCancelled` and `kPending` (returned by the console's
synchronous `Execute` when an asynchronous command did not finish inline; `data`
carries the job id). A command with `result_schema` is tested against it.

Console variables are commands too: the built-ins `cvar.get`, `cvar.set`
and `cvar.list` take and return typed values, and the console's
`name value` shorthand is parsed into `cvar.set`. Command discovery and
jobs are built-ins as well ([§3.5](#35-built-in-commands)).

### 3.4 Dispatch and jobs

The engine owns dispatch, because it owns the command registry, the
console and the frame:

```cpp
// IAsyncEngine
//! Queue an invocation; it starts at the next frame start, in order.
virtual auto PostCommand(CommandInvocation invocation, CommandContext context)
  -> JobId = 0;
//! Queue an invocation and await its result.
virtual auto RunCommand(CommandInvocation invocation, CommandContext context)
  -> co::Co<CommandResult> = 0;
virtual auto CancelCommand(JobId job) -> bool = 0;
```

- Every invocation, from any source, starts at the engine's frame start on
  the engine thread, in arrival order, after latched console variables are
  applied. A click therefore takes effect at most one frame later than
  today, and a remote command never runs in the middle of a frame phase.
- A synchronous handler completes at that point. An asynchronous handler
  runs as a job in the engine's command nursery and may await frames, loads
  or other jobs; `CancelCommand` cancels its coroutine, and the command
  decides what cancellation undoes (cancelling `ngin.session.begin` ends the
  session).
- Jobs are numbered per process. A job finishes with a `CommandResult`, which
  goes to whoever awaits it and into a `command` event.
- `CommandRegistry::Run` is the primitive the dispatcher calls; tests use
  the registry directly, without an engine.

The engine adds the awaitables its own commands need: `ngin.step` replies
when its frames have run (or `kCancelled` when `ngin.resume` cancels them),
and `ngin.session.end` replies when the session has ended.

### 3.5 Built-in commands

| Command                   | Effect                                                                                      | Registered by |
| ------------------------- | ------------------------------------------------------------------------------------------- | ------------- |
| `help [name]`             | As today, from the parameter list.                                                          | Console       |
| `find`, `list`            | As today.                                                                                   | Console       |
| `exec <path>`             | As today: runs a file of console lines.                                                     | Console       |
| `cvar.get <name>`         | Typed value, origin, latched and restart values.                                            | Console       |
| `cvar.set <name> <value>` | Sets a variable; the `name value` shorthand.                                                | Console       |
| `cvar.list [pattern]`     | Variables with type, range, allowed values and flags.                                       | Console       |
| `cmd.list [pattern]`      | Commands with label, help, parameter schema, result schema, flags and current availability. | Commands      |
| `cmd.jobs`                | Running jobs: id, command, source, start frame.                                             | Engine        |
| `cmd.cancel <job>`        | Cancels a job.                                                                              | Engine        |
| `ngin.quit`               | Stops the engine; the process exits. The window close button posts it.                      | Engine        |

### 3.6 Key bindings

`Oxygen.Host` owns a key binding table: chord → invocation. Modules
register default bindings next to the commands they bind
(`ViewportToolbar` registers F1 and Alt+1…; the console UI registers
backtick and Ctrl+Shift+P for `ui.console.toggle` and
`ui.palette.toggle`). Bindings fire only when ImGui does not want text
input, as today. Menus and tooltips show a command's chord by looking it up
in the table, so the shortcut is declared once. User rebinding is not in R1.

### 3.7 Source policy

Policy is per source and lives in `Oxygen.Commands`; it applies to every
caller, not only the console.

- The remote source runs every command and sets every variable, the same as
  the local console; `kRemoteAllowed` and the remote allowlist are removed.
  The control server is a development facility, protected by loopback and a
  token ([§4.2](#42-endpoint-token-and-discovery)).
- `kDevOnly` and `kCheat` gating for shipping builds is unchanged and now
  covers every source. A shipped game's console lists, completes and runs
  only the commands its policy allows; development commands are neither
  visible nor runnable there, while the game's own UI can still run the
  commands it needs.

## 4. Control server

`host::ControlServer`, in `Oxygen.Host`, off unless the application enables
it (`HostConfig::control`, the `--control [port]` option of `RuntimeCli`,
or the player's launch file).

### 4.1 Transport and threading

- TCP, bound to `127.0.0.1` only. Port `0` (the default) takes an ephemeral
  port; `--control <port>` fixes it.
- Asio's `ip::tcp` on the platform's `io_context`, written as OxCo
  coroutines with `co::asio_awaitable`: accept loop, one read loop and one
  write queue per connection, all in the host nursery. No extra thread; the
  host event loop already polls that `io_context` on the engine thread.
- A request is read, parsed and validated on the engine thread, then posted
  to the dispatcher; its reply is written when the job's result is known.
  A client may have many requests in flight; replies carry their ids and
  may arrive in any order.
- Limits: 1 MiB per incoming line (larger closes the connection), 8
  connections. No idle timeout: agents and editors stay connected between
  turns.

### 4.2 Endpoint, token and discovery

On start the server writes a discovery file to the per-user local data
folder (`%LOCALAPPDATA%\Oxygen\control\<pid>.json` on Windows, the
`XDG_RUNTIME_DIR` equivalent elsewhere), and deletes it on clean exit:

```json
{
  "protocol": 1,
  "pid": 41236,
  "application": "Oxygen.Player",
  "port": 50513,
  "token": "9f2c…(64 hex digits)",
  "started": "2026-10-13T14:02:11Z"
}
```

- The token is 256 random bits per run. Only processes of the same user can
  read the file; that, plus loopback, is the access control.
- A launcher knows the pid it started and waits for that file. A client
  attaching later lists the folder; a file whose pid is not running is
  stale and is deleted by whoever finds it.

### 4.3 Connection lifetime and ownership

- The first message on a connection must be `rpc.hello` with the token;
  anything else, or a wrong token, closes the connection.
- A client may claim **ownership** in `rpc.hello`; there is at most one
  owner. When the application was started with
  `exit_when_owner_leaves` (the player's launch file, for the editor and
  `play_scene`), losing the owner's connection posts `ngin.quit`, and so
  does no owner arriving within 10 seconds of start. Otherwise the
  application keeps running and accepts new clients.
- A client's disconnect does not cancel the jobs it started; their results
  still reach the event stream. `rpc.cancel` cancels explicitly.

## 5. Protocol

[JSON-RPC 2.0](https://www.jsonrpc.org/specification), UTF-8, one JSON
object per line (`\n`), no batches. Method names starting with `rpc.` are
reserved by JSON-RPC for the protocol itself; every other method is a
command.

### 5.1 Connection methods

| Method            | Params                                                             | Result                                                                              |
| ----------------- | ------------------------------------------------------------------ | ----------------------------------------------------------------------------------- |
| `rpc.hello`       | `token`, `protocol` (1), `client` (name), `owner` (bool, optional) | `protocol`, `application`, `pid`, `frame`, `event_seq` (latest), `topics`.          |
| `rpc.subscribe`   | `topics` (array)                                                   | Topics now subscribed. Notifications for them start with the next event.            |
| `rpc.unsubscribe` | `topics`                                                           | Topics still subscribed.                                                            |
| `rpc.events`      | `since` (seq), `topics` (optional), `limit` (optional, ≤ 1000)     | `events`, `next_seq`, `dropped` (events older than the buffer that were asked for). |
| `rpc.cancel`      | `id` (of an earlier request on this connection)                    | `cancelled` (bool).                                                                 |

These are connection plumbing, not interactions: they change only what this
connection receives.

### 5.2 Requests and replies

```json
{"jsonrpc":"2.0","id":7,"method":"ngin.step","params":{"frames":3}}
{"jsonrpc":"2.0","id":7,"result":{"output":"","data":{"frame":1842}}}
```

The reply comes when the command finishes: `ngin.step` after its three
frames, `ngin.session.begin` when the scene has loaded or failed. Clients
set their own timeouts.

| Outcome                    | JSON-RPC error                                              |
| -------------------------- | ----------------------------------------------------------- |
| Line is not JSON           | `-32700` parse error                                        |
| Not a valid request        | `-32600`                                                    |
| Unknown command            | `-32601`                                                    |
| Arguments break the schema | `-32602`, `data.errors`: schema messages with JSON pointers |
| `kDisabled`                | `-32001`, `data.reason`                                     |
| `kDenied`                  | `-32002`                                                    |
| `kCancelled`               | `-32003`                                                    |
| `kError`                   | `-32000`, `message` from `error`, `data.output`             |

### 5.3 Commands as methods

A command's name is its method name and its parameter schema is the
method's `params` schema; `cmd.list` returns all of them, with current
availability, so a client needs no other description of the protocol.
Adding a command adds a method; the protocol version changes only when the
framing, the `rpc.*` methods or the event envelope change.

## 6. Events

```json
{
  "jsonrpc": "2.0",
  "method": "rpc.event",
  "params": {
    "seq": 5121,
    "frame": 1840,
    "topic": "lifecycle",
    "data": { "state": "scene_loaded", "scene": "/.cooked/Scenes/Level.oscene" }
  }
}
```

`seq` increases by one per event across all topics. The server keeps the
last 4,096 events in a ring buffer, whether or not anyone is subscribed, so
`rpc.events` can return what a client missed between turns.

| Topic        | Data                                                                                                                                          | Published by                            |
| ------------ | --------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| `lifecycle`  | `starting`, `session_started`, `scene_loaded`, `scene_failed` (with issues), `paused`, `resumed`, `session_ended`, `stopping`                 | engine/host; scene states by the player |
| `command`    | Job id, source (`local_console`, `ui`, `key`, `remote:<client>`, `config`, `automation`), name, arguments, status, start and end frame, error | dispatcher                              |
| `log`        | Severity, channel, text, as the player's `log` event today                                                                                    | host log capture                        |
| `diagnostic` | A [script diagnostic](../scripting/script-diagnostics.md#2-model)                                                                             | scripting handlers                      |
| `gpu_work`   | A [GPU work summary](../vortex/lld/gpu-work-summary.md#41-types), on each new revision                                                        | host                                    |

`ui` and `key` are new `CommandSource` values, so the console's audit hook
and the `command` event distinguish a click from a typed line.

`gpu_work` events exist only while some client subscribes to the topic: the
first subscription takes a GPU timeline lease and the last unsubscribe or
disconnect releases it, replacing the player's `stats` request.

Event data is defined by `oxygen.control-events.schema.json`, installed with
the SDK; the player's events schema is retired.

## 7. `Oxygen.Player`

- Launch file `version` 2: `diagnostics_pipe` is replaced by an optional
  `control` object, `{ "port": 0, "exit_when_owner_leaves": true }`. With
  `control` present the player starts the server; `--control [port]` starts
  it from the command line. Version 1 files are rejected with the schema
  error.
- `ngin.session.begin [scene]` is the one Play: it remounts loose cooked
  roots and begins the session (what the pipe's `play` did), so the toolbar's
  Play also shows recooked content. It is asynchronous and returns the
  scene's load outcome: `data` has the scene, the camera it is viewed
  through and the instantiation issues; a failed load returns `kError` with
  the same data after `session_ended`.
- Removed: `PipeChannel`, the request parser and its variant, the requests
  schema, the events schema and the module's request handling. The log
  capture moves from `EventStream` into the control server.
- The pipe's requests map to commands:

| Pipe request       | Command                                        |
| ------------------ | ---------------------------------------------- |
| `play {scene}`     | `ngin.session.begin [scene]`                   |
| `pause`, `resume`  | `ngin.pause`, `ngin.resume`                    |
| `step {frames}`    | `ngin.step [frames]`                           |
| `end_session`      | `ngin.session.end`                             |
| `stop`             | `ngin.quit`                                    |
| `stats {gpu_work}` | `rpc.subscribe` / `rpc.unsubscribe` `gpu_work` |

## 8. Consumers

- **Editor Play** ([script authoring §8](../../../../design/editor/lld/script-authoring.md#8-play)):
  writes `control` instead of a pipe name, reads the discovery file of the
  player it started, connects as owner and subscribes to `lifecycle`, `log`
  and `diagnostic`. Play runs `ngin.session.begin`, Stop runs
  `ngin.session.end`, Close player runs `ngin.quit`. The editor has no pipe
  code yet, so this changes its design only.
- **`Examples/Content/play_scene`**: writes the `control` block and connects
  as owner only when asked to stream events; otherwise it omits `control`
  and the player runs standalone.
- **Tests and agents** attach to any running host application through its
  discovery file. The Python harness used to validate P1 becomes a small
  client under `tools/` with the same JSON-RPC client the MCP server (R2)
  will use.

## 9. Exit gates

| ID   | Check                                                                                                                                                                                                                                                                                                               |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RC-1 | Commands and console tests: text and JSON arguments produce identical `CommandArgs` and identical errors; defaults, ranges, enums and `rest_of_line`; generated schemas are valid JSON Schema; a disabled command returns `kDisabled` with its reason.                                                              |
| RC-2 | Engine tests: invocations start at frame start in arrival order from every source; an asynchronous job completes, is cancelled by `CancelCommand`, and is cancelled at shutdown; `ngin.step 3` completes after three simulated frames and `ngin.resume` cancels it.                                                 |
| RC-3 | Every registered command (engine, content, graphics, scripting, host, player, DemoShell, LightBench) has a label and parameters; `cmd.list` in the player validates against its schema.                                                                                                                             |
| RC-4 | Control server tests with an in-process client: wrong or missing token closes the connection; requests in flight complete out of order; `rpc.events` returns missed events and reports drops; an oversized line closes the connection; the discovery file appears and is removed on exit; a stale file is detected. |
| RC-5 | Player over the protocol: SIP-3 to SIP-6 rerun as commands (play, pause, step replying after its frames, end session, quit, `gpu_work` subscription and lease release on disconnect); owner disconnect quits; a non-owner disconnect does not.                                                                      |
| RC-6 | No bypass: clicking the toolbar's Play, F1 and the console button each produce a `command` event with source `ui` or `key` seen by a remote client; closing the window produces `ngin.quit`.                                                                                                                        |
| RC-7 | `play_scene` and the recook check of P1 pass through the protocol.                                                                                                                                                                                                                                                  |

## 10. Decisions

Binding for R1.

| ID    | Decision                                                                                                                                                      | Rationale and implication                                                                                                                                                                                                                                                             |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RC-D1 | Each command is its own JSON-RPC method; there is no generic `cmd.run {name, args}` method.                                                                   | Errors map onto JSON-RPC codes per method, params schemas are per method, and the protocol needs no second envelope. The MCP server (R2) still exposes a few generic tools (`command_list`, `command_run`) rather than one tool per command; that is its mapping, not the protocol's. |
| RC-D2 | Every source, UI included, starts its commands at frame start.                                                                                                | One ordering and one execution point for every source; a click takes effect at most one frame later. Executing UI commands immediately and remote ones at frame start would make a click and the same remote request behave differently.                                              |
| RC-D3 | A client's disconnect does not cancel the jobs it started; only `rpc.cancel` does.                                                                            | A session load started by an agent does not depend on its connection. Owner loss with `exit_when_owner_leaves` quits the application anyway.                                                                                                                                          |
| RC-D4 | `kRemoteAllowed` and the remote allowlist are removed; every command is remotely invocable.                                                                   | Access control is loopback plus the token. Shipping-build policy for the control server is outside R1.                                                                                                                                                                                |
| RC-D5 | Commands live in a new module, `Oxygen.Commands`. Console variables stay in `Oxygen.Console`, which is a text front end over commands and registers `cvar.*`. | Commands serve every caller and span frames; the console is one front end, ships in games and must stay small ([§3](#3-command-model)).                                                                                                                                               |
| RC-D6 | The launch file moves to version 2; version 1 is rejected.                                                                                                    | The current-format rule; nothing outside the repository writes launch files.                                                                                                                                                                                                          |

## 11. Not in scope

- **R2, MCP server**: a Python process (the repository's `uv` workspace) that
  launches or attaches to host applications through discovery files and
  exposes `command_list`, `command_run`, event catch-up and, after R3,
  screenshots as MCP tools.
- **R3, observation and input**: frame capture returning PNG, scene queries
  (nodes, transforms, components), game input injection through
  `InputSystem`, Lua evaluation, deterministic stepping with a fixed frame
  time. Each is a command.
- Control from another machine (binding beyond loopback, TLS), shipping
  builds, user key rebinding, command recording and replay as a feature, and
  the editor's in-process engine exposing the same server.

## 12. Documents changed with R1

| Document                                                                                            | Change                                                                                                                    |
| --------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| [scene instantiation and player](scene-instantiation-and-player.md) §3, §5                          | Launch `control` replaces `diagnostics_pipe`; the event stream section points here; SIP-5 and SIP-6 restated as commands. |
| [runtime host](runtime-host.md) summary, §6, §7, §9, P-5                                            | "through the pipe" becomes the control server; R1 row before E2; E2's gate adds the rule of §2 for DemoShell panels.      |
| [script authoring §8](../../../../design/editor/lld/script-authoring.md#8-play)                     | Flow steps 4–6 per [§8](#8-consumers).                                                                                    |
| [ED-M08](../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md) M08.10m | "event stream" becomes the control protocol.                                                                              |
| `src/Oxygen/Commands/README.md` (new)                                                               | Command model, parameters, results, availability, sources and policy.                                                     |
| `src/Oxygen/Console/README.md`                                                                      | The console as a text front end over commands; console variables and `cvar.*`; built-ins.                                 |
