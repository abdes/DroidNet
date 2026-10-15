# Commands and remote control

Status: **approved 2026-10-13; decisions RC-D1 to RC-D9 taken 2026-10-13. New
ground: this design is expected to change as R1 is built, and changes are
recorded here as they are made ([§13](#13-changes-since-approval))**. Delivery
step R1 of [runtime host §9](runtime-host.md#9-delivery-order-and-exit-gates),
before E2. Decisions are in [§10](#10-decisions).

Summary:

- **Every interaction is a command.** A toolbar button, a menu item, a key
  binding, the command palette, a console line, closing the window and a remote
  request all run a registered command through one dispatcher. No UI code
  changes engine, host or scene state any other way ([§2](#2-the-rule)).
- Commands get their own module, `Oxygen.Commands`; the console becomes a text
  front end over it and keeps console variables. A command declares typed
  parameters, a label, availability and a structured result. The same
  declaration gives the console its grammar and help and gives remote callers a
  JSON Schema. A command may take several frames; it then runs as a job that can
  be awaited and cancelled ([§3](#3-command-model)).
- The engine owns only the command registry. Dispatch, jobs, the event log and
  the control server live in a new module, `Oxygen.Control`, composed by the
  host ([§3.4](#34-dispatch-and-jobs), [§4](#4-control-server)).
- Local front ends (toolbar, keys, console, palette) post to the dispatcher in
  process. The control server is the remote front end: JSON-RPC 2.0, one JSON
  object per line, over loopback TCP, on the platform's `io_context` on the
  engine thread. Every command is a JSON-RPC method of the same name
  ([§4](#4-control-server), [§5](#5-protocol)).
- Events (lifecycle, log, script diagnostics, executed commands, GPU work) are
  numbered notifications kept in a ring buffer, so a client can catch up after a
  gap ([§6](#6-events)).
- The player's named pipe, its request schema and its reader thread are removed;
  the editor and `play_scene` use the control server ([§7](#7-oxygenplayer),
  [§8](#8-consumers)).
- An MCP server for agents (R2) and frame capture, scene queries and input
  injection (R3) build on this and are not part of R1 ([§11](#11-not-in-scope)).

## 1. Findings

Recorded before R1; the code they cite has since changed.

| Finding                                                                                                                                                                                                                                                                              | Where                                                                             |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------- |
| The viewport toolbar already acts only through console lines: view mode, show flags, stats and Play/Pause/Step/Stop each run a command or set a console variable.                                                                                                                    | `Host/ViewportToolbar.cpp`                                                        |
| Some interactions bypass commands: the toolbar's console button and the backtick and Ctrl+Shift+P hotkeys change `ConsoleUiState` directly; closing the window calls `engine->Stop()`.                                                                                               | `Host/ViewportToolbar.cpp`; `ImGui/Console/ConsoleHost.cpp`; `Host/RunHost.cpp`   |
| The player's pipe requests call the engine directly. `play` remounts the content but the toolbar's Play (`ngin.session.begin`) does not, so one interaction has two behaviours. `stop` and `stats` have no command at all.                                                           | `Player/PlayerModule.cpp`                                                         |
| Commands are text in, text out and synchronous. Handlers receive `std::vector<std::string>` and parse it themselves; nothing describes their parameters, results are text, and a command whose effect takes frames (`ngin.step 3`, a scene load) returns before the effect is known. | `Console/Command.h`; `Engine/AsyncEngine.cpp`                                     |
| The console already models command sources (`kRemote`, `kAutomation`, …), per-source policy and an audit hook. Remote execution is opt-in per command (`kRemoteAllowed`), and no command sets it, so nothing is remotely invocable.                                                  | `Console/Registry.h`                                                              |
| UI availability is ad hoc: the toolbar decides that sessions exist by checking whether `ngin.session.begin` is registered.                                                                                                                                                           | `Host/ViewportToolbar.cpp`                                                        |
| The platform owns an `asio::io_context`, polled by the host's event loop on the engine thread; OxCo already adapts Asio operations into awaitables with cancellation.                                                                                                                | `Platform/Platform.h`; `Platform/AsyncOps.cpp`; `Host/RunHost.cpp`; `OxCo/asio.h` |
| About 35 commands are registered: engine, content, graphics, scripting, the player, and DemoShell's `pp.*` and LightBench's `lightbench.*`.                                                                                                                                          | `RegisterCommand` call sites                                                      |

## 2. The rule

An **interaction** is anything a user or a program does that changes the state
of the engine, the scene, the host or its debug UI: play state, view mode, show
flags, panel visibility, console variables, content mounts, quitting. Every
interaction runs a command through the command dispatcher
([§3.4](#34-dispatch-and-jobs)), whatever started it:

| Origin                      | How it runs a command                                                                                        |
| --------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Toolbar button, menu item   | The widget posts a command line when clicked (source `ui`).                                                  |
| Key binding                 | The owner of the chord posts a command line when it is pressed (source `key`; [§3.6](#36-key-bindings)).     |
| Console line, palette entry | The line is posted as typed (source `local_console`).                                                        |
| Window close button         | Posts `ngin.quit`.                                                                                           |
| Owner client leaving        | Posts `ngin.quit` when the application exits with its owner ([§4.3](#43-connection-lifetime-and-ownership)). |
| Launch file, `-c`, `exec`   | Console variable sets and command lines, as before.                                                          |
| Remote client               | A JSON-RPC request whose method is the command name ([§5.3](#53-commands-as-methods)).                       |

Local front ends post to the dispatcher in process; only remote clients go
through the control server. A key press or a console line never becomes
JSON-RPC.

Not interactions, and not commands: presentation inside a widget (typing in the
console's input field, moving the palette cursor, hovering, scrolling, opening a
dropdown), and game input. Game input goes from `InputSystem` to the scene's
input mapping contexts and never passes through the dispatcher; injecting game
input from a program is itself a command (R3).

Consequences:

- Anything a person can do in the window, a remote client can do, with the same
  validation and the same result.
- Every executed command is an event with its source ([§6](#6-events)), so a
  remote client sees what a person clicked, and a person's session can be
  replayed from its command events.
- A widget never decides availability itself: it reads the command's
  availability and shows the reason when disabled.

The rule binds every host-based application. E2 converts DemoShell's panels: a
panel control that changes settings or state posts a command or sets a console
variable; panel layout and widget presentation stay local.

## 3. Command model

Commands live in their own module, `Oxygen.Commands`. A command is broader than
a console line: a toolbar, a key, a game's own menu or a remote client runs it
with typed arguments and may await it across frames. The console is one text
front end among those callers, and it also ships in games, where it must stay
small and expose only what a shipped build allows.

| Module            | Owns                                                                                                                                                                                     | Depends on                      |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------- |
| `Oxygen.Commands` | Command definitions, parameters and their schemas, arguments, results, availability, sources and per-source policy, the audit hook, the registry, `Run` and `cmd.list`                   | Base, OxCo, nlohmann json       |
| `Oxygen.Console`  | Console variables (registry, latching, persistence, startup plan), the text front end (parser, history, completion, execution records), `help`, `find`, `list`, `exec`, `cvar.*`         | Base, Config, Commands          |
| `Oxygen.Engine`   | The registry and console instances, the `RegisterCommands` module hook, the run state the run-state commands drive                                                                       | both                            |
| `Oxygen.Control`  | The dispatcher and its jobs, the run-state and job commands, the event log and log capture, the control server ([§3.4](#34-dispatch-and-jobs), [§4](#4-control-server), [§6](#6-events)) | Commands, Console, Engine, Asio |

The console turns a text line into invocations and runs them through the command
registry; it no longer stores commands. Console variables stay in the console,
and the console registers the `cvar.*` commands that read and set them. The
free-form `std::vector<std::string>` handler is removed, and every registered
command is migrated in R1 (no second command path).

Modules register their commands from the `EngineModule::RegisterCommands` hook,
called once when the module is attached, and unregister them when they shut
down. Services that are not modules (graphics, the asset loader, script
compilation) register from the engine.

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
  CommandHandler handler;            // completes immediately
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

The types live in namespace `oxygen::commands`. `CommandContext` carries the
source, whether the build is a shipping build, the client name for remote
sources and the `JobId` of the running invocation. `CommandRegistry::Register`
returns a `CommandHandle` that unregisters the command.

### 3.2 Parameters

```cpp
struct CommandParam {
  std::string name;   // "frames"
  ParamType type;     // kBool, kInt, kDouble, kString, kEnum, kPath, kScalar
  std::string help;
  bool required { false };
  std::optional<ParamValue> default_value;
  std::optional<double> min_value, max_value;
  std::vector<std::string> allowed_values; // kEnum
  bool rest_of_line { false }; // last string param only: `find`, `exec`
};
```

- **One parameter list, two syntaxes.** Console text is positional in
  declaration order, with `name=value` allowed for any parameter; JSON callers
  pass an object. `BindText` and `BindJson` both produce the same `CommandArgs`,
  typed, defaulted and range-checked before the handler runs. Handlers no longer
  parse text.
- **Schema.** The registry builds each command's JSON Schema from its parameters
  (`additionalProperties: false`) for clients and tools. The parameter list is
  itself the validator: text and JSON are both converted through it, so their
  errors match and `Oxygen.Commands` needs no schema validator library.
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

`ExecutionStatus` gains `kDisabled` (availability said no; `error` holds the
reason), `kCancelled` and `kPending` (returned by the registry's synchronous
`Execute` for an asynchronous command, which only a dispatcher can run). A
command with `result_schema` is tested against it.

Console variables are commands too: the built-ins `cvar.get`, `cvar.set` and
`cvar.list` take and return typed values, and the console's `name value`
shorthand is parsed into `cvar.set`. Command discovery and jobs are built-ins as
well ([§3.5](#35-built-in-commands)).

A console line may hold several commands separated by `;`. The whole line is
parsed and bound before any of it runs, so a mistake anywhere in the line runs
nothing; the commands then run in order and stop at the first failure.

### 3.4 Dispatch and jobs

The engine stays lean: it owns the command registry because every module
registers into it, and nothing else of dispatch. `control::CommandDispatcher`
runs every invocation. It is a `co::LiveObject` owned by the host and activated
in the host's nursery, so its jobs outlive module teardown;
`control::CommandDispatchModule`, an engine module at the highest priority in
the frame-start phase, ticks it once per frame.

```cpp
// control::CommandDispatcher
//! Queue an invocation; it starts at the next frame start.
auto Post(CommandInvocation invocation, CommandContext context) -> JobId;
//! Queue a console line; parse errors are recorded at once.
auto PostLine(std::string line, CommandContext context) -> void;
//! Queue an invocation and await its result; abandoning the await cancels it.
auto Run(CommandInvocation invocation, CommandContext context)
  -> co::Co<CommandResult>;
auto Cancel(JobId job) -> bool;
//! Resumes at the next frame start; for asynchronous handlers.
auto NextFrame() -> co::Co<>;
```

- Every invocation, from any source, starts at frame start on the engine thread,
  so a remote command never runs in the middle of a frame phase and a click
  takes effect at most one frame later.
- Jobs start in arrival order, and each runs until it first waits before the
  next one starts, so their effects apply in arrival order: `ngin.step 5` then
  `ngin.resume` in the same frame requests the steps, then cancels them.
- A synchronous handler completes at that point. An asynchronous handler keeps
  running as a job in the dispatcher's nursery and may await frames, loads or
  other jobs; `Cancel` cancels its coroutine, and the command decides what
  cancellation undoes. Jobs still running when the host stops are cancelled.
- Jobs are numbered per process. A job finishes with a `CommandResult`, which
  goes to whoever awaits it, into the console's execution records when it came
  from a line, and into a `command` event.
- Each job is logged when it starts and when it finishes, and a job still
  running after 600 frames is reported once, so a stuck command is visible.
- `CommandRegistry::Run` is the primitive the dispatcher calls; tests use the
  registry directly, without an engine.

The dispatcher registers the run-state commands, because their replies need
frames. `ngin.step` replies when its frames have run: the engine reports how
many requested steps have not yet started (`IAsyncEngine::GetPendingSteps`),
which reaches zero when they have all begun or when `ngin.resume` or a session
change cancels them, so the wait always ends. Its `data` holds the frames
requested and the frames that ran; fewer is `kCancelled`. `ngin.session.end`
replies when the session has ended.

### 3.5 Built-in commands

| Command                                           | Effect                                                                                                                            | Registered by |
| ------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ------------- |
| `help [name]`                                     | From the parameter list.                                                                                                          | Console       |
| `find`, `list`                                    | Search and list commands and variables.                                                                                           | Console       |
| `exec <path>`                                     | Runs a file of console lines.                                                                                                     | Console       |
| `cvar.get <name>`                                 | Typed value, origin, latched and restart values.                                                                                  | Console       |
| `cvar.set <name> <value>`                         | Sets a variable; the `name value` shorthand.                                                                                      | Console       |
| `cvar.list [pattern]`                             | Variables with type, range, allowed values and flags.                                                                             | Console       |
| `cmd.list [pattern]`                              | Commands with label, help, parameter schema, result schema, flags and current availability.                                       | Commands      |
| `cmd.jobs`                                        | Running jobs: id, command, source, start frame.                                                                                   | Control       |
| `cmd.cancel <job>`                                | Cancels a job.                                                                                                                    | Control       |
| `ngin.pause`, `ngin.resume`, `ngin.step [frames]` | Run state; `ngin.resume` cancels pending steps.                                                                                   | Control       |
| `ngin.session.end`                                | Ends the play session; replies when it has ended.                                                                                 | Control       |
| `ngin.quit`                                       | Stops the engine; the process exits. The window close button posts it.                                                            | Control       |
| `ui.console [action]`, `ui.palette [action]`      | Show, hide or focus the console and the palette (`toggle` by default).                                                            | Console UI    |
| `host.window.size <width> <height>`               | Resizes the window's framebuffer, in physical pixels, restoring it first when maximized; disabled while full screen or minimized. | Host          |
| `host.window.mode <windowed\|borderless>`         | Frames the window, or covers its display at the desktop resolution without changing the display mode.                             | Host          |

### 3.6 Key bindings

A key binding is owned by the code that owns the chord, and pressing it posts a
command line with source `key`. The viewport toolbar binds F1 and Alt+1…; the
console UI binds backtick (`ui.console open`, `close` or `focus`, decided from
what the user sees when the key is pressed) and Ctrl+Shift+P (`ui.palette`).
Bindings fire only when ImGui does not want text input, except backtick, which
always reaches the console. A shared binding table that menus and tooltips read,
and user rebinding, are not in R1.

### 3.7 Source policy

Policy is per source and lives in `Oxygen.Commands`; it applies to every caller,
not only the console.

- The remote source runs every command and sets every variable, the same as the
  local console; `kRemoteAllowed` and the remote allowlist are removed. The
  control server is a development facility, protected by loopback and a token
  ([§4.2](#42-endpoint-token-and-discovery)).
- `kDevOnly` and `kCheat` gating for shipping builds is unchanged and now covers
  every source. A shipped game's console lists, completes and runs only the
  commands its policy allows; development commands are neither visible nor
  runnable there, while the game's own UI can still run the commands it needs.
- Console history records only lines typed in the console (`local_console`).

## 4. Control server

`control::ControlServer`, in `Oxygen.Control`, off unless the application
enables it (`HostConfig::control`, the `--control [port]` option, or the
player's launch file). Like the dispatcher it is a `co::LiveObject` owned by the
host and activated in the host's nursery.

### 4.1 Transport and threading

- TCP, bound to `127.0.0.1` only. Port `0` (the default) takes an ephemeral
  port; `--control <port>` fixes it. `--control` is a runtime option of every
  host application.
- Asio's `ip::tcp` on the platform's `io_context` (`AsyncOps::IoContext()`),
  written as OxCo coroutines with `co::asio_awaitable`: an accept loop, one read
  loop and one write queue per connection, all in the server's nursery. No extra
  thread; the host event loop already polls that `io_context` on the engine
  thread.
- A request is read, parsed and bound on the engine thread, then posted to the
  dispatcher; its reply is written when the job's result is known. A client may
  have many requests in flight; replies carry their ids and may arrive in any
  order.
- Limits: 1 MiB per incoming line (larger closes the connection), 8 connections
  (more are refused). No idle timeout: agents and editors stay connected between
  turns.

### 4.2 Endpoint, token and discovery

On start the server writes a discovery file to the per-user local data folder
(`%LOCALAPPDATA%\Oxygen\control\<pid>.json` on Windows,
`$XDG_RUNTIME_DIR/Oxygen/control` elsewhere), and deletes it on clean exit. It
writes the file aside and renames it into place, so a client that finds the file
reads it whole. When the engine has completed, the host awaits
`ControlServer::FlushAsync` (at most half a second) before the connections
close, so clients receive the last events, such as the player's `stopping`:

```json
{
  "protocol": 1,
  "pid": 41236,
  "application": "Oxygen Player",
  "name": "left",
  "started": "2026-10-13T17:23:04Z",
  "command_line": "Oxygen.Player.exe --control --control-name left",
  "port": 50513,
  "token": "9f2c…(64 hex digits)"
}
```

- The token is 256 random bits per run. Only processes of the same user can read
  the file; that, plus loopback, is the access control.
- A launcher knows the pid it started and waits for that file. A client
  attaching later lists the folder and picks an instance by its `name`, the
  label the launcher gave with `--control-name`; instances of one application
  differ only by pid otherwise. The label also ends the window title
  (`MultiView Example [left]`), so a person can match a window to its file.
  `started` and `command_line` describe an unlabelled instance.
- A killed application cannot delete its file. Each server removes the files of
  processes that no longer run when it starts, and a client that finds one
  deletes it.
- Instances of one application share its saved state (console variables,
  history, settings). The first to start owns it; a later one starts from the
  saved state but does not save, and logs a warning, so it cannot overwrite the
  owner's changes.

### 4.3 Connection lifetime and ownership

- The first message on a connection must be `rpc.hello` with the token; anything
  else, or a wrong token, is answered with an error and closes the connection.
- A client may claim **ownership** in `rpc.hello`; there is at most one owner.
  When the application was started with `exit_when_owner_leaves` (the player's
  launch file, for the editor and `play_scene`), losing the owner's connection
  posts `ngin.quit`, and so does no owner arriving within 10 seconds of start.
  Otherwise the application keeps running and accepts new clients.
- A client's disconnect does not cancel the jobs it started; their results still
  reach the event stream. `rpc.cancel` cancels explicitly.

## 5. Protocol

[JSON-RPC 2.0](https://www.jsonrpc.org/specification), UTF-8, one JSON object
per line (`\n`), no batches. Method names starting with `rpc.` are reserved by
JSON-RPC for the protocol itself; every other method is a command.

### 5.1 Connection methods

| Method            | Params                                                         | Result                                                                              |
| ----------------- | -------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `rpc.hello`       | `token`, `client` (name), `owner` (bool, optional)             | `protocol`, `application`, `name`, `pid`, `owner` (granted), `event_seq` (latest).  |
| `rpc.subscribe`   | `topics` (array)                                               | Topics now subscribed. Notifications for them start with the next event.            |
| `rpc.unsubscribe` | `topics`                                                       | Topics still subscribed.                                                            |
| `rpc.events`      | `since` (seq), `topics` (optional), `limit` (optional, ≤ 1000) | `events`, `next_seq`, `dropped` (events older than the buffer that were asked for). |
| `rpc.cancel`      | `id` (of an earlier request on this connection)                | `cancelled` (bool).                                                                 |

These are connection plumbing, not interactions: they change only what this
connection receives.

### 5.2 Requests and replies

```json
{"jsonrpc":"2.0","id":7,"method":"ngin.step","params":{"frames":3}}
{"jsonrpc":"2.0","id":7,"result":{"output":"stepped 3 frame(s)","data":{"frames":3,"ran":3}}}
```

The reply comes when the command finishes: `ngin.step` after its three frames,
`ngin.session.begin` when the scene has loaded or failed. Clients set their own
timeouts. A request without an `id` is a notification: the command runs and
nothing is replied. A failed command's error `data` holds its `status`, its
`output` and any data it returned.

| Outcome                    | JSON-RPC error                                  |
| -------------------------- | ----------------------------------------------- |
| Line is not JSON           | `-32700` parse error                            |
| Not a valid request        | `-32600`                                        |
| Unknown command            | `-32601`                                        |
| Arguments break the schema | `-32602`, with the binding error                |
| `kDisabled`                | `-32001`, `message` gives the reason            |
| `kDenied`                  | `-32002`                                        |
| `kCancelled`               | `-32003`                                        |
| `kError`                   | `-32000`, `message` from `error`, `data.output` |

### 5.3 Commands as methods

A command's name is its method name and its parameter schema is the method's
`params` schema; `cmd.list` returns all of them, with current availability, so a
client needs no other description of the protocol. Adding a command adds a
method; the protocol version changes only when the framing, the `rpc.*` methods
or the event envelope change.

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

`control::EventLog` numbers events: `seq` increases by one per event across all
topics. It keeps the last 4,096 events in a ring buffer, whether or not anyone
is subscribed, so `rpc.events` can return what a client missed between turns.
Publishing is thread-safe; the server pushes notifications from the engine
thread.

| Topic        | Data                                                                                                                                              | Published by      |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------- |
| `command`    | Job id, source (`local_console`, `ui`, `key`, `remote`, `config`, `automation`), client, commands and arguments, line, status, start frame, error | dispatcher        |
| `log`        | Severity, channel, text                                                                                                                           | event log capture |
| `lifecycle`  | `session_started`, `scene_loaded`, `scene_failed` (with issues), `paused`, `resumed`, `session_ended`, `stopping`                                 | player            |
| `diagnostic` | A [script diagnostic](../scripting/script-diagnostics.md#2-model)                                                                                 | player            |
| `gpu_work`   | A [GPU work summary](../vortex/lld/gpu-work-summary.md#41-types), on each new revision                                                            | player            |
| `stats`      | `fps`, whole frames per second, when the engine's sampled rate changes                                                                            | player            |

`ui` and `key` are new `CommandSource` values, so the audit hook and the
`command` event distinguish a click from a typed line.

`gpu_work` and `stats` events exist only while some client subscribes to the
topic: the event log counts interest per topic, and the player holds a GPU
timeline lease only while `gpu_work` has interest, replacing the pipe's `stats`
request.

Each publisher defines its topics' data: the player's in
`oxygen.player-events.schema.json` (`{topic, data}`, installed with the SDK),
the `command` and `log` topics in `Oxygen.Control`'s README.

## 7. `Oxygen.Player`

- Launch file `version` 2: `diagnostics_pipe` is replaced by an optional
  `control` object, `{ "port": 0, "exit_when_owner_leaves": true }`. With
  `control` present the player starts the server; `--control [port]` starts it
  from the command line. Version 1 files are rejected with the schema error.
- `ngin.session.begin [scene]` is the one Play: it reads the launch file again,
  remounts the loose cooked roots it names and begins the session (what the
  pipe's `play` did), so the toolbar's Play also shows recooked content. It is
  asynchronous and replies with the scene's load outcome: `data` has the scene
  and the camera it is viewed through; a failed load returns `kError` with the
  issues.
- Removed: `PipeChannel`, `EventStream`, the request parser and its variant, the
  requests schema and the module's request handling. The log capture moves into
  the event log.
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

- **Editor Play**
  ([script authoring §8](../../../../design/editor/lld/script-authoring.md#8-play)):
  writes `control` instead of a pipe name, reads the discovery file of the
  player it started, connects as owner and subscribes to `lifecycle`, `log` and
  `diagnostic`. Play runs `ngin.session.begin`, Stop runs `ngin.session.end`,
  Close player runs `ngin.quit`. The editor has no pipe code yet, so this
  changes its design only.
- **`Examples/Content/play_scene`**: writes a version 2 launch file without
  `control`, so the player runs standalone; `-- --control` makes it accept
  clients.
- **Tests and agents** attach to any running host application through its
  discovery file. The Python JSON-RPC client arrives with the MCP server (R2),
  which uses it.

## 9. Exit gates

| ID   | Check                                                                                                                                                                                                                                                                                                  |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| RC-1 | Commands and console tests: text and JSON arguments produce identical `CommandArgs` and identical errors; defaults, ranges, enums and `rest_of_line`; generated schemas are valid JSON Schema; a disabled command returns `kDisabled` with its reason; a line with an error anywhere runs nothing.     |
| RC-2 | Control tests: invocations start at frame start and take effect in arrival order; an asynchronous job completes, is cancelled by `cmd.cancel` and by an abandoned awaiter; `ngin.step 2` completes after two simulated frames, and `ngin.resume` cancels it, also when paused again in the same frame. |
| RC-3 | Every registered command (engine, content, graphics, scripting, host, player, DemoShell, LightBench) has a label and parameters; `cmd.list` in the player validates against its schema.                                                                                                                |
| RC-4 | Control server tests with an in-process client: wrong or missing token closes the connection; requests in flight complete out of order; `rpc.events` returns missed events and reports drops; an oversized line closes the connection; the discovery file appears and is removed on exit.              |
| RC-5 | Player over the protocol: SIP-3 to SIP-6 rerun as commands (play, pause, step replying after its frames, end session, quit, `gpu_work` subscription and lease release on disconnect); owner disconnect quits; a non-owner disconnect does not.                                                         |
| RC-6 | No bypass: clicking the toolbar's Play, F1 and the console button each produce a `command` event with source `ui` or `key` seen by a remote client; closing the window produces `ngin.quit`.                                                                                                           |
| RC-7 | `play_scene` writes a version 2 launch file and plays its scene; with `-- --control` the player accepts clients.                                                                                                                                                                                       |

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
| RC-D7 | The engine owns only the command registry; dispatch, jobs, the event log and the control server live in `Oxygen.Control`, composed by the host.               | The engine owns engine-wide resources and functionality comes from modules. The dispatcher's jobs live in the host's nursery, so they outlive module teardown and are cancelled when the host stops.                                                                                  |
| RC-D8 | Local front ends post to the dispatcher in process; only remote clients use JSON-RPC. Game input never goes through the dispatcher.                           | A key press or a console line pays no serialization or socket cost, and per-frame player input pays no dispatch cost. Injecting game input remotely is an R3 command.                                                                                                                 |
| RC-D9 | Jobs take effect in arrival order: each starts only once the previous one first waits.                                                                        | Pipelined requests (`ngin.step 5` then `ngin.resume`) behave as sent, whatever the handlers' await depth; long jobs still run concurrently once they wait.                                                                                                                            |

## 11. Not in scope

- **R2, MCP server**: a Python process (the repository's `uv` workspace) that
  launches or attaches to host applications through discovery files and exposes
  `command_list`, `command_run`, event catch-up and, after R3, screenshots as
  MCP tools. Its JSON-RPC client is the repository's Python client for the
  protocol.
- **R3, observation and input**: frame capture returning PNG, scene queries
  (nodes, transforms, components), game input injection through `InputSystem`,
  Lua evaluation, deterministic stepping with a fixed frame time. Each is a
  command.
- Control from another machine (binding beyond loopback, TLS), shipping builds,
  a shared key binding table and user rebinding, command recording and replay as
  a feature, and the editor's in-process engine exposing the same server.

## 12. Documents changed with R1

| Document                                                                                            | Change                                                                                                                    |
| --------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| [scene instantiation and player](scene-instantiation-and-player.md) §3, §5                          | Launch `control` replaces `diagnostics_pipe`; the event stream section points here; SIP-5 and SIP-6 restated as commands. |
| [runtime host](runtime-host.md) summary, §6, §7, §9, P-5                                            | "through the pipe" becomes the control server; R1 row before E2; E2's gate adds the rule of §2 for DemoShell panels.      |
| [script authoring §8](../../../../design/editor/lld/script-authoring.md#8-play)                     | Flow steps 4–6 per [§8](#8-consumers).                                                                                    |
| [ED-M08](../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md) M08.10m | "event stream" becomes the control protocol.                                                                              |
| `src/Oxygen/Commands/README.md` (new)                                                               | Command model, parameters, results, availability, sources and policy.                                                     |
| `src/Oxygen/Console/README.md`                                                                      | The console as a text front end over commands; console variables and `cvar.*`; built-ins.                                 |
| `src/Oxygen/Control/README.md` (new)                                                                | Dispatcher, jobs, event log and topics, control server.                                                                   |

## 13. Changes since approval

| Change                                                                                                                 | Why                                                                                                                                      |
| ---------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Dispatch moved from `IAsyncEngine` (`PostCommand`, `RunCommand`) to `Oxygen.Control`, with the control server (RC-D7). | The engine stays lean; the dispatcher and server are LiveObjects in the host's nursery, using OxCo and the platform's Asio integration.  |
| Local front ends post lines in process (RC-D8).                                                                        | Typing in the console or pressing a key must not go through the server.                                                                  |
| No shared key binding table in R1; each chord's owner posts its command ([§3.6](#36-key-bindings)).                    | The rule needs every chord to run a command, not a central table; the table matters once menus show shortcuts and users rebind keys.     |
| Jobs take effect in arrival order (RC-D9).                                                                             | Every `co_await` is an executor hop, so jobs started together interleaved by await depth and `ngin.resume` could act before `ngin.step`. |
| `ngin.step` ends on the engine's pending step count and reports the frames that ran.                                   | Inferring the end from pause state could wait forever when the steps were cancelled and the engine paused again in the same frame.       |
| A console line is bound completely before any of it runs.                                                              | A line is one request: a typo in its last command no longer runs the first ones.                                                         |
| Event data schemas belong to their publishers; there is no `oxygen.control-events.schema.json`.                        | The player's topics keep its installed schema; `Oxygen.Control` documents its own topics.                                                |
| The discovery file has no start time, and `rpc.hello` returns no frame or topic list.                                  | Nothing consumes them; the event sequence number already gives a client its catch-up point.                                              |
