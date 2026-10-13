# Oxygen Commands

`Oxygen.Commands` defines what a command is and keeps the registry every
caller runs commands through: the console, toolbars, key bindings and remote
clients. A command declares typed parameters, a label, availability and a
structured result; the same declaration gives the console its grammar and
help and gives remote callers a JSON Schema. The design is
[commands and remote control](../../../design/runtime-core/commands-and-remote-control.md).

It depends only on Base, OxCo and nlohmann json; it has no UI and no engine
dependency.

## Defining a command

```cpp
#include <Oxygen/Commands/CommandRegistry.h>

using namespace oxygen::commands;

const auto handle = registry.Register(CommandDefinition {
  .name = "gfx.capture",
  .label = "Capture Frame",
  .help = "Capture the next frame to a file",
  .params = {
    CommandParam {
      .name = "path",
      .type = ParamType::kPath,
      .help = "Output file",
      .required = true,
    },
  },
  .handler = [](const CommandArgs& args, const CommandContext&) {
    // args.GetString("path") is bound, typed and validated.
    return CommandResult::Success("capture requested");
  },
});
```

- **Name**: module-prefixed (`ngin.*`, `gfx.*`, `cntt.*`); it is also the
  JSON-RPC method name.
- **Handler**: exactly one of `handler` (completes immediately) or
  `async_handler` (an OxCo coroutine that may span frames; only a dispatcher
  such as `Oxygen.Control`'s can run it).
- **Availability**: an optional function returning `enabled` and a `reason`;
  a disabled command returns `kDisabled` with that reason, and UI reads it to
  grey out controls.
- **Result**: `status`, human-readable `output`, `error`, and structured
  `data` for programs; `result_schema` optionally describes `data`.
- **Handle**: `Register` returns a `CommandHandle`; unregister it when the
  owner goes away. Engine modules register from
  `EngineModule::RegisterCommands`.

## Parameters and binding

Parameter types are `kBool`, `kInt`, `kDouble`, `kString`, `kEnum`
(`allowed_values`), `kPath` and `kScalar` (any scalar, interpreted by the
handler). Each may be `required`, have a `default_value` and `min_value` /
`max_value`; the last string parameter may take the `rest_of_line`.

- `BindText(name, tokens)`: console tokens, positional in declaration order,
  or `param=value`.
- `BindJson(name, params)`: a JSON object, as remote clients send it.

Both produce the same `CommandArgs` and the same errors, so text and JSON
callers see one validation. `Describe` returns a command's label, help,
flags, generated parameter JSON Schema (`additionalProperties: false`),
result schema and current availability.

## Running

- `Execute(invocation, context)`: synchronous; an asynchronous command
  returns `kPending`.
- `Run(invocation, context)`: a coroutine that runs either kind; what a
  dispatcher calls.

Statuses are `kOk`, `kNotFound`, `kInvalidArguments`, `kDenied` (source
policy), `kDisabled` (availability), `kError` (a handler failure or an
exception it threw), `kCancelled` and `kPending`.

## Sources and policy

`CommandContext` carries the source (`kLocalConsole`, `kConfigFile`,
`kRemote`, `kAutomation`, `kUi`, `kKey`), whether the build is a shipping
build, the remote client's name and the dispatcher's job id.
`SetSourcePolicy` decides per source whether commands may run and whether
`kDevOnly` and `kCheat` commands are allowed; `SetAuditHook` observes every
outcome with its source and client.

## Built-ins

`cmd.list [pattern]` lists commands as `Describe` does, with current
availability. Console built-ins (`help`, `find`, `list`, `exec`, `cvar.*`)
are registered by [`Oxygen.Console`](../Console/README.md); dispatcher and
run-state commands by [`Oxygen.Control`](../Control/README.md).

## Testing

`Test/` covers parameter binding from text and JSON, generated schemas,
availability, source policy and synchronous and asynchronous runs.
