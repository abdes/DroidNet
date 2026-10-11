# Script parameter declarations — ownership decision

Status: **decided 2026-10-11: option B** (owner decision SD-1). It is the
basis of [scene script hosting](scene-script-hosting.md) and the editor's
[script authoring LLD](../../../../design/editor/lld/script-authoring.md).

Summary: every surveyed engine except Roblox declares a script's editable
parameters in the script itself and lets the engine or its toolchain extract
them. Oxygen should do the same: a literal `oxygen.script { params = {...} }`
declaration that the engine parses statically with the Luau parser, cooks into
the script asset, and serves to the editor through one analysis API.

## 1. The question

A script slot carries typed parameter values (`ScriptParamRecord`). Something
must state which keys a script accepts, their types, defaults and authoring
hints, so that the inspector can show one field per parameter and the runtime
can supply defaults. Today nothing does: keys and types exist only in the
values a sidecar happens to list, scripts hard-code their own fallbacks
(`scene.param(ctx, "speed") or 1.6`), and `ScriptLoader` always passes the
asset an empty default list.

## 2. Industry practice

| Engine          | Where parameters are declared                                                               | Who extracts them                                                    |
| --------------- | ------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| Unity           | Public or `[SerializeField]` fields of the script class                                     | Engine serializer reflects the compiled assembly                     |
| Unreal          | `UPROPERTY(EditAnywhere)` members; Blueprint variables marked Instance Editable             | Engine reflection (UnrealHeaderTool, Blueprint compiler)             |
| Godot           | `@export` annotations, with hints such as `@export_range` and `@export_enum`                | GDScript compiler; the editor reads the script's property list       |
| Defold (Lua)    | Top-level `go.property("name", default)` calls                                              | Build step parses the literal declarations; values are not evaluated |
| O3DE (Lua)      | A `Properties` table inside the script table, with default, description and unit attributes | Engine evaluates the script table in a tools context                 |
| CryEngine (Lua) | An entity script's `Properties` table                                                       | Engine reads the table from the script                               |
| Roblox (Luau)   | Instance attributes, set per instance in Studio; scripts read them with `GetAttribute`      | Nobody: there is no per-script schema                                |

The consensus is clear: the script owns its parameter declarations, and the
engine (or its build toolchain) extracts them. Roblox is the exception and has
the weaknesses Oxygen shows today: misspelled keys silently read `nil`, and
each script repeats its defaults in code.

## 3. Options

**A. Editor-owned schema.** A descriptor beside the source
(`Orbit.oscript.json`) lists keys, types, defaults and hints, edited in a
Script inspector.

**B. Script-declared, statically extracted by the engine (recommended).** The
script declares its parameters in a literal Luau table. `Oxygen.Scripting`
parses it with the Luau AST (no execution), reports violations as
diagnostics, cooks key, type and default into the script asset, and serves the
full declaration, hints included, to tools.

**C. Script-declared, extracted by executing the script.** Tools run the chunk
in a sandbox and read the resulting table (the O3DE approach).

| Criterion                        | A. Editor schema                                   | B. Static declaration                             | C. Executed declaration                                     |
| -------------------------------- | -------------------------------------------------- | ------------------------------------------------- | ----------------------------------------------------------- |
| Single source of truth           | No: schema and code drift apart                    | Yes                                               | Yes                                                         |
| Runtime defaults                 | Only if the engine also reads the descriptor       | Yes, cooked into the asset                        | Yes                                                         |
| Works outside the editor         | Sidecars and RenderScene must repeat the schema    | Any tool using the engine API                     | Any tool using the engine API                               |
| Slots added at runtime by script | No defaults                                        | Defaults from the asset                           | Defaults from the asset                                     |
| Code to write                    | Editor descriptor, schema, inspector, cooker input | Engine analyzer, format records, one Interop call | Analyzer plus a sandboxed VM in tools and cooker            |
| Safety and determinism           | Safe                                               | Safe: parse only                                  | Runs user code at cook time; can hang or depend on bindings |
| Expressiveness                   | Any                                                | Literal values only (Defold's rule)               | Any expression                                              |
| Industry alignment               | Unusual                                            | Defold, Godot (static annotations)                | O3DE                                                        |

Rejected: **A** duplicates the contract outside the code that reads it and
leaves every non-editor producer without a schema. **C** puts a script VM and
the binding packs in the cooker and the editor's analysis path to gain
computed defaults, which authors rarely need.

## 4. Recommended declaration

```lua
local Orbit = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1.6, min = 0, max = 10,
      unit = "m/s", description = "Orbit speed" },
    target = oxygen.param.node { description = "Node to orbit around" },
    orb_material = oxygen.param.asset { kind = "material" },
    toggle = oxygen.param.asset { kind = "input_action" },
    tint = oxygen.param.vec3 { default = { 1, 1, 1 }, color = true },
    mode = oxygen.param.string { default = "loop", options = { "loop", "once" } },
  },
}

function Orbit:on_start() end
function Orbit:on_gameplay(dt) end

return Orbit
```

Rules the analyzer enforces, each with its own diagnostic:

1. One top-level `oxygen.script { ... }` call per script, assigned to a local
   that the chunk returns.
2. `params` is a table constructor; each entry is
   `name = oxygen.param.<type> { ... }` with a literal argument table.
3. Field values are literals: numbers (with unary minus), strings, booleans
   and tables of literals. A computed value is an error pointing at the
   expression, with help suggesting the literal.
4. Keys are identifiers of at most 63 bytes; strings fit the record limits.
5. `default` matches the type; `min`, `max` and `step` apply to numeric
   types; `options` to strings; `color` to `vec3`/`vec4`; `kind` is required
   for `asset`.

Types: `bool`, `int`, `float`, `string`, `vec2`, `vec3`, `vec4`, `asset`
(with a required asset kind) and `node` (a node of the same scene). The asset
and node types are required for scripts to work as authored: GUID strings
are invisible to cook dependency closure, packaging and rename retargeting,
and node names break on rename (see the hosting LLD, section 3).

At runtime, `oxygen.script` returns the script table and records the same
declaration, so the hosting layer can validate overrides and fill defaults
when a script was loaded from source rather than a cooked asset.

## 5. Consequences

- `ScriptAssetDesc` gains a declaration table; the script asset version
  increases and maintained content is recooked.
- The editor reads declarations through Interop from the source on disk, so
  the inspector reflects an edit as soon as the file is saved, before any cook.
- Hints (`min`, `max`, `unit`, `description`, `options`, `color`) are tools
  data. They are not cooked; tools read them from source.
- `scene.param(ctx, key)` and the `ctx` helpers are removed; scripts read
  `self.params.<name>`.

## Sources

- [Defold: script properties](https://defold.com/manuals/script-properties)
- [O3DE: Lua Properties table](https://docs.o3de.org/docs/user-guide/scripting/lua/properties/)
- [Godot: GDScript exported properties](https://docs.godotengine.org/en/4.5/tutorials/scripting/gdscript/gdscript_exports.html)
- [Unity: script serialization](https://docs.unity3d.com/cn/2023.2/Manual/script-Serialization.html)
- [CryEngine: structure of a script entity](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/23306594)
- [Roblox: properties and attributes](https://create.roblox.com/docs/scripting/attributes)
