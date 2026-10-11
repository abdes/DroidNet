# Script diagnostics

Status: **design; owner decisions SD-1–SD-7 taken on 2026-10-11**. Part of
[scene script hosting](scene-script-hosting.md).

Summary: every script problem, whether found while parsing, compiling,
analyzing declarations, linting, type checking, attaching or running, becomes
one `ScriptDiagnostic`: a code, a message, labeled source spans, notes and an
optional suggested fix. Producers only adapt their source error into that
model; one renderer prints it the way `rustc` does; the editor shows the same
structure in its own UI. The aim is an error a user can act on without
reading engine logs.

## 1. What the user sees

```text
error[S0302]: parameter default must be a literal value
  --> Content/Scripts/Orbit.lua:4:42
   |
 3 |   params = {
 4 |     speed = oxygen.param.float { default = base * 2 },
   |                                            ^^^^^^^^ computed here
   |
   = note: declarations are read without running the script
   = help: write the value, for example `default = 3.2`
```

```text
error[S0701]: attempt to index nil with 'set_position'
  --> Content/Scripts/Follow.lua:18:5
   |
18 |     self.params.target:set_position(p)
   |     ^^^^^^^^^^^^^^^^^^ this is nil
   |
   = note: in on_gameplay, node "Camera Rig", slot 1
   = note: parameter `target` is not set on this slot
   = help: set Target in the node's Scripting section
```

The second example shows binding context turned into help: the runtime knows
that the nil value came from an unset `node` parameter.

## 2. Model

```cpp
enum class DiagnosticSeverity : uint8_t { kError, kWarning, kNote };

struct SourceSpan {
  std::string source_name; // virtual or project-relative path
  Position begin;          // 1-based line and UTF-8 column
  Position end;            // exclusive
};

struct DiagnosticLabel {
  SourceSpan span;
  std::string message; // may be empty
  bool primary;        // exactly one primary label when any span is known
};

struct Suggestion {
  std::string message;
  std::optional<SourceSpan> span;     // replaced range, if machine-applicable
  std::optional<std::string> replacement;
};

struct ScriptDiagnostic {
  DiagnosticSeverity severity;
  DiagnosticCode code;   // S0302
  std::string message;   // one line, lower case, no trailing period
  std::vector<DiagnosticLabel> labels;
  std::vector<std::string> notes;
  std::optional<Suggestion> help;
  std::optional<SlotContext> slot; // node, slot, hook for binding/runtime
};
```

`DiagnosticCode` indexes one constexpr catalog: code, family, title and a
short explanation that the editor shows on demand (the `rustc --explain`
idea). Codes are stable; retired codes are never reused.

| Range | Family                 | Producer                                                                                                 |
| ----- | ---------------------- | -------------------------------------------------------------------------------------------------------- |
| S01xx | Syntax                 | Luau parser (`ParseResult::errors`)                                                                      |
| S02xx | Compile                | Luau compiler (`CompileError`)                                                                           |
| S03xx | Declarations           | `ScriptAnalyzer` rules ([declaration rules](script-parameter-declarations.md#4-recommended-declaration)) |
| S04xx | Lint                   | Luau linter (`LintWarning` codes mapped one to one)                                                      |
| S05xx | Types                  | Luau type checker against the Oxygen API definitions                                                     |
| S06xx | Binding and attachment | `SceneScriptAttachment`, sidecar validation, source resolution                                           |
| S07xx | Runtime                | Hook errors with their traceback                                                                         |

## 3. Producers

Each producer is a small adapter from one error source to `ScriptDiagnostic`.
None of them formats text.

- **Parse, compile, lint, types.** Luau locations convert to `SourceSpan`
  (Luau is 0-based). The Luau message becomes the diagnostic message; the
  adapter adds help only for known messages (for example, a missing `end`
  labels the opening keyword as a secondary span).
- **Declarations.** The analyzer emits each rule's code with the offending
  expression as primary span and the rule's help.
- **Binding.** Attachment and sidecar validation name the slot and, when the
  source is available, label the declaration the override failed against.
- **Runtime.** Chunks are named `@<source_name>`, so Luau error positions and
  `lua_Debug` frames map back to source spans. The innermost frame in the
  script is the primary label; outer frames become notes. Known patterns get
  binding-aware help, such as indexing a `nil` `self.params` field that
  belongs to an unset `node` or `asset` parameter.

## 4. Rendering and transport

- `RenderText(const ScriptDiagnostic&, const SourceProvider&)` produces the
  layout in section 1: header, location arrow, gutter, source lines with
  underlines and label text, then notes and help. A `SourceProvider` returns
  source lines by name and keeps a line index per source; when a source is
  unavailable the renderer prints the header, location and notes only. ANSI
  color is an option of the console sink, not of the renderer.
- Logs, the engine console and the player write rendered text.
- Tools receive the structure: Interop converts `ScriptDiagnostic` to managed
  records, and the player streams it as JSON (`oxygen.script-diagnostic.schema.json`)
  over its diagnostics channel. The editor renders the same fields itself.
- A faulted instance produces one runtime diagnostic, not one per frame
  (existing fault policy). A reload that fails publishes its diagnostics once
  per source revision.

## 5. Type checking

Linting catches unknown globals and unused locals but not a misspelled API
member (`oxygen.scene.fnd_one`). Type checking does, and needs Luau
definitions for the whole `oxygen` API. By owner decision (2026-10-11) it is
V0.1 scope on condition that the definitions cost no separate maintenance.
That is why nobody writes `oxygen.d.luau`: the build generates it.

**One registration, one signature.** Packs stop calling
`lua_pushcfunction`/`lua_setfield` directly (about 220 call sites today).
Each pack declares its functions, userdata classes and methods in tables:

```cpp
constexpr auto kTimeFunctions = std::to_array<BindingFunction>({
  { "delta_seconds", LuaTimeDeltaSeconds, "() -> number" },
  { "is_paused", LuaTimePaused, "() -> boolean" },
});
```

One shared helper registers a table into Lua; the same tables describe the
pack. A binding cannot be registered without a signature, so adding a
function is one row that carries its type.

**Generated, never edited.** A build step links the packs, collects their
descriptions and writes `oxygen.d.luau` into the build tree and the SDK.
Shared types (`Vec3`, `Quat`, `SceneNode`, `AssetRef`) are declared once by
the pack that owns the userdata.

**Three tests keep it honest.**

1. Coverage: a test walks the live `oxygen` table after registration and
   fails on any function, class or method without a description, so a raw
   registration cannot slip back in.
2. Validity: Luau parses the generated definitions; a malformed signature
   fails the build.
3. Truth: the maintained example scripts and each pack's Luau test scripts
   are type-checked against the definitions and also executed. A signature
   that disagrees with its implementation fails one of the two in a test
   that uses the function.

Physics packs are not described until physics authoring; their namespaces are
declared `any`, so physics calls are not checked and raise no false errors.
Scripts are checked in Luau's non-strict mode by default, so unannotated code
gets errors only where inference is certain; `--!strict` opts in to more.

`ScriptAnalyzer` loads the generated file for level `kTypes`. The same file
configures `luau-lsp` in the user's code editor, which then offers
completion and the same type errors while typing. `oxygen.script` is typed so
that `self.params` carries the declared parameter types into hook bodies.

## 6. Exit gates

| ID    | Check                                                                                                             |
| ----- | ----------------------------------------------------------------------------------------------------------------- |
| SDG-1 | Golden rendered output for one diagnostic of each family, including multi-line spans and missing sources.         |
| SDG-2 | A runtime error inside a nested function reports the script line and the hook, node and slot.                     |
| SDG-3 | The JSON form validates against its schema and round-trips through Interop.                                       |
| SDG-4 | The coverage, validity and example type-check tests pass; type checking flags a misspelled `oxygen.scene` member. |
