# Oxygen Lua Bindings Developer Guide

This guide covers the design, naming conventions, lifecycle rules, and integration
patterns for Lua bindings in `src/Oxygen/Scripting/Bindings`. It is written for
contributors adding or maintaining bindings and reflects the current Luau C API
implementation (not sol2).

Companion tests live in `src/Oxygen/Scripting/Test`.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Naming Conventions](#2-naming-conventions)
3. [Context Model](#3-context-model)
4. [Table vs. Userdata](#4-table-vs-userdata)
5. [No-Leak Policy](#5-no-leak-policy)
6. [Registration Patterns](#6-registration-patterns)
7. [Error Handling and Stack Discipline](#7-error-handling-and-stack-discipline)
8. [Phase Safety and Mutation Rules](#8-phase-safety-and-mutation-rules)
9. [Adding a New Binding](#9-adding-a-new-binding)
10. [Reference Files](#10-reference-files)
11. [Common Pitfalls](#11-common-pitfalls)

---

## 1. Architecture Overview

### 1.1 Pack and Namespace Structure

Bindings are grouped into **packs**. Each pack implements
`contracts::IScriptBindingPack` and registers one or more **namespaces** under
the `oxygen` global table.

```text
oxygen.scripting.bindings
├── Contracts/
│   └── IScriptBindingPack.h        ← interface every pack implements
├── BindingRegistry.h/.cpp           ← RegisterBindingNamespaces()
├── LuaBindingCommon.h/.cpp          ← shared context, tag dtors, push/check helpers
└── Packs/
    ├── Core/                        ← oxygen.app, .conventions, .events, .hash,
    │                                   .log, .math, .time, .uuid
    ├── Content/                     ← oxygen.assets
    ├── Input/                       ← oxygen.input
    ├── Physics/                     ← oxygen.physics.body, .character, .query,
    │                                   .events, .constants
    └── Scene/                       ← oxygen.scene
```

### 1.2 Namespace Map

| Pack | Lua namespaces |
| --- | --- |
| `Core` | `oxygen.app`, `oxygen.conventions`, `oxygen.events`, `oxygen.hash`, `oxygen.log`, `oxygen.math`, `oxygen.time`, `oxygen.uuid` |
| `Content` | `oxygen.assets` |
| `Input` | `oxygen.input` |
| `Physics` | `oxygen.physics.body`, `oxygen.physics.character`, `oxygen.physics.query`, `oxygen.physics.events`, `oxygen.physics.constants` |
| `Scene` | `oxygen.scene` |

### 1.3 Registration Flow

`ScriptingModule` creates and freezes the runtime environment, then calls every
registered pack's `Register` method during sandbox initialization.

```text
ScriptingModule::Init()
  └─ for each IScriptBindingPack:
       pack.Register({ lua_state, runtime_env_ref })
         └─ RegisterBindingNamespaces(state, runtime_env_ref, namespaces)
              ├─ ensure runtime env table  (via runtime_env_ref)
              ├─ ensure oxygen{}           (create if absent)
              ├─ ensure oxygen.__namespaces{}
              ├─ for each BindingNamespace:
              │    descriptor.register_fn(state, oxygen_table_index)
              └─ mark oxygen.__namespaces[name] = true
```

`BindingNamespace` is a plain struct: `{ const char* name, NamespaceRegisterFn register_fn }`.
The `name` field is recorded in `oxygen.__namespaces` for runtime introspection.

---

## 2. Naming Conventions

### 2.1 C++ Side

| Concept | Convention | Example |
| --- | --- | --- |
| Pack registration entry point | `CreateXBindingPack()` | `CreateCoreBindingPack()` |
| Namespace registration function | `RegisterXBindings(state, oxygen_idx)` | `RegisterEventsBindings` |
| Lua C callback (internal, anonymous ns) | `LuaXVerb` | `LuaEventsOn`, `LuaSceneCreateNode` |
| Metatable registration | `RegisterXMetatable(state)` | `RegisterSceneNodeMetatable` |
| Push helper (puts userdata on stack) | `PushX(state, ...)` | `PushSceneNode`, `PushQuat` |
| Check helper (validates type, returns ptr) | `CheckX(state, idx)` | `CheckSceneNode`, `CheckQuat` |
| Metatable name constant | `k...Metatable` | `kSceneNodeMetatable = "oxygen.scene.node"` |

All Lua C callbacks are defined in anonymous namespaces inside the `.cpp` file
that owns them. They are never `extern`.

### 2.2 Lua Surface

All public names under `oxygen.<namespace>` and on userdata metatables are
**snake_case**.

```lua
oxygen.time.delta_seconds()
oxygen.scene.create_node(name, parent?)
oxygen.assets.get_material(guid)

node:get_name()
node:set_local_position(vec)
node:attach_point_light(params)
```

`SceneNode` exposes property aliases for convenience (handled via `__index` /
`__newindex`). The aliases call the underlying `get_*` / `set_*` methods
transparently; they are **not** additional fields stored on the userdata.

### 2.3 Legacy Guards

Debug builds install a `__index` guard on selected module tables (currently
`oxygen.scene` and `oxygen.assets`). Accessing a removed v0 name raises an
explicit migration error such as:

```text
oxygen.scene.find was removed in v1; use oxygen.scene.find_one / find_many / query
```

Do not re-introduce removed names, even as aliases.

---

## 3. Context Model

There are three distinct context objects, each with a different lifetime and
storage strategy.

### 3.1 Runtime Context (Registry-Backed, Frame Lifetime)

`LuaRuntimeContext` is stored as tagged userdata in the Lua registry under the
key `__oxgn_runtime_context` (tag: `kTagRuntimeContext`, metatable:
`oxygen.scripting.runtime_context`). It holds:

- `observer_ptr<engine::FrameContext>` — the active frame context
- `observer_ptr<IAsyncEngine>` — the active engine

`ScriptingModule` calls `SetActiveEngine` once at engine-attach time and
null-clears it at shutdown. The `FrameContext` pointer is **not** set
globally for the full engine phase; it is pushed and restored around each
discrete scoped block inside the phase handler (via `ScopedActiveFrameContext`).
Bindings that call `GetActiveFrameContext` receive a null observer outside those
scoped blocks. On shutdown, both are null-cleared before the Lua state is closed.

Binding code retrieves them with `GetActiveFrameContext(state)` /
`GetActiveEngine(state)`. Both return a null observer on failure; callers must
handle that case.

### 3.2 Binding Context (Per-Script-Invocation)

`PushScriptContext(state, slot_context*, dt_seconds)` pushes a Lua table that
serves as the script's `self` argument. The table contains:

- `__oxgn_binding_context` — tagged userdata (`kTagBindingContext`) holding
  a `LuaBindingContext` (slot handle + `dt_seconds`).
- Compatibility helpers: `GetParam`, `SetLocalRotationEuler`, `SetLocalRotation`,
  `GetDeltaSeconds`.

Modern code should use `oxygen.*` namespaces for everything except the
context argument itself, which remains necessary for slot/script execution
plumbing.

`GetBindingContextFromScriptArg(state, arg_index)` retrieves the context and
validates the tag before returning the pointer.

### 3.3 Events Runtime (Registry-Backed, Sandbox Lifetime)

`EventRuntime` is stored as tagged userdata under `__oxgn_events_runtime`
(tag: `kTagEventRuntime`, metatable: `oxygen.events.runtime`).

It is lazily created on first use (`EnsureRuntime`) and must be explicitly
shut down with `ShutdownEventsRuntime(state)` before the Lua state is closed,
so that all pending `lua_ref` handles for listeners and queued payloads are
released in a deterministic order.

```text
EventRuntime
  ├── buckets: event_name → phase_name → EventBucket
  │                                       └── sorted EventListener[]
  ├── id_map:  listener_id → ListenerLocation
  ├── queue:   QueuedEvent[]
  └── stats:   event_name → EventStats
```

---

## 4. Table vs. Userdata

Choose the representation before writing any C++ code.

| Use **tables** for | Use **userdata** for |
| --- | --- |
| Plain value aggregates / option structs | Opaque engine handles (`SceneNode`, `SceneQuery`) |
| Module singletons (`oxygen.scene`, `oxygen.assets`) | Types with metamethod behaviour (`quat`, `mat4`) |
| Lightweight connection objects (events connection) | Objects with identity semantics |
| Simple callback closures | Types owning heap-backed C++ state |

**Vec3** uses the Luau native vector primitive (`lua_pushvector` / `lua_isvector`
/ `lua_tovector`). Do not wrap it in a table or custom userdata.

> The codebase currently mixes tagged and untagged userdata in older code. All
> new bindings must follow Section 5.

---

## 5. No-Leak Policy

Luau silently ignores the `__gc` metamethod. The **only** correct destructor
mechanism for C++ objects attached to userdata is the tagged destructor API.

### 5.1 Lifecycle Rules at a Glance

```text
Non-trivial type                   Trivial type
(shared_ptr, string, vector, …)    (raw ptr, observer_ptr, POD handles)
─────────────────────────────────  ────────────────────────────────────
lua_newuserdatatagged(tag)         lua_newuserdata (no tag)
lua_setuserdatadtor(tag, Dtor)     — no destructor needed —
new (mem) T { … }
luaL_getmetatable / lua_setmetatable
```

> Luau supports tags 0–127. Tags are allocated in `LuauUserdataTag` in
> `LuaBindingCommon.h`. Leave gaps between subsystem ranges for future growth.

Current tag allocations:

| Range | Subsystem |
| --- | --- |
| 10–12 | Scripting contexts (`kTagBindingContext`, `kTagRuntimeContext`, `kTagEventRuntime`) |
| 20–22 | Math types (`kTagVec4`, `kTagQuat`, `kTagMat4`) |
| 30–31 | Scene (`kTagSceneNode`, `kTagSceneQuery`) |
| 40–42 | Content resources and assets (`kTagTextureResource`, `kTagBufferResource`, `kTagAsset`) |

> **Physics pack uses no tags.** All four physics handle types
> (`PhysicsBodyHandleUserdata`, `PhysicsCharacterHandleUserdata`,
> `PhysicsBodyIdUserdata`, `PhysicsCharacterIdUserdata`) are trivially
> destructible POD structs storing only `NamedType<uint32_t>` fields.
> They use untagged `lua_newuserdata` and require no destructor.

### 5.2 Destructor Pattern

The destructor signature accepted by `lua_setuserdatadtor` is
`void(lua_State*, void*)`, not `int(lua_State*)`. The sole responsibility of the
destructor is to invoke the C++ type's destructor on the raw memory; it must not
call back into Lua.

```cpp
void MyTypeDtor(lua_State* /*state*/, void* data)
{
    static_cast<MyType*>(data)->~MyType();
}
```

Register the destructor **once per tag**, inside `RegisterXMetatable`, before
any userdata of that tag is allocated.

### 5.3 Shared Layout Across Types

A single tag may cover multiple layout-compatible types that share the same
destructor. The Content pack uses this: all asset variants
(`MaterialAsset`, `GeometryAsset`, `ScriptAsset`, `InputActionAsset`,
`InputMappingContextAsset`) are stored inside a single `AssetUserdata` union-like
struct under `kTagAsset`, differentiated by `AssetUserdataKind`.

### 5.4 `lua_ref` Hygiene

When acquiring a reference with `lua_ref`:

- Unref on every failure path before returning from the C function.
- Unref after dispatching a callback (events: after each `InvokeListener` call
  for `once` listeners; after batch dispatch for payloads).
- Unref during explicit shutdown (`ShutdownEventsRuntime` unrefs all listener
  callbacks and queued payload refs before clearing the runtime).

The events binding is the canonical example to study for `lua_ref` lifetime
management.

---

## 6. Registration Patterns

### 6.1 Namespace Registration

Every `RegisterXBindings(state, oxygen_table_index)` follows this stack discipline:

```text
1. PushOxygenSubtable(state, oxygen_table_index, "x")
   → pushes or creates oxygen.x, returns absolute stack index
2. lua_pushcfunction + lua_setfield  (repeat for each public function)
3. lua_pop(state, 1)                 (pop the module table)
```

`PushOxygenSubtable` normalizes negative stack indices before use, so it is safe
to call with either absolute or relative indices.

### 6.2 Metatable Registration

```text
luaL_newmetatable(state, kXMetatable)
lua_setuserdatadtor(state, kTagX, XDtor)   ← non-trivial types only
lua_pushcfunction / lua_setfield           ← __tostring, __eq, __index, __newindex
for each method:
    lua_pushcclosure(state, fn, name, 0)
    lua_setfield(state, -2, name)
lua_pop(state, 1)
```

Register metatables **before** any userdata of that type is pushed. For packs
with multiple userdata types (e.g. Scene), metatable registration is called first
thing in `RegisterXBindings`, before the module table is pushed.

### 6.3 SceneNode Property Aliases

`SceneNode` demonstrates the property-alias pattern: `__index` intercepts
known keys (e.g. `"name"`, `"local_position"`) and delegates to the
corresponding `get_*` method without storing a field on the userdata. `__newindex`
mirrors this for writable properties and rejects read-only ones with a hard error.

```lua
-- Read aliases (call get_* internally)
node.name           -- → :get_name()
node.local_position -- → :get_local_position()
node.world_position -- → :get_world_position()

-- Write aliases (call set_* internally)
node.name = "foo"           -- → :set_name("foo")
node.local_position = vec   -- → :set_local_position(vec)

-- Read-only properties raise a hard error on write
node.world_position = vec   -- luaL_error: "SceneNode property 'world_position' is read-only"
```

---

## 7. Error Handling and Stack Discipline

### 7.1 Error Strategy

Engine pattern:

| Situation | Approach |
| --- | --- |
| Programmer contract violation (wrong type, wrong arity, malformed argument shape) | **Hard Lua error** via `luaL_error` / `luaL_argerror` |
| Runtime absence / domain state (missing entity, missing asset, unavailable optional context) | **Soft return** (`nil`/`false`/empty result), with logging where operationally useful |
| Forbidden operation by rule (phase restriction, reserved names) | **Hard Lua error** |
| Engine invariant breach / impossible state | **`CHECK_F` (fail-fast)** |

Hard errors are intentional and script authors may use `pcall(...)` to assert
negative paths in tests and validation scripts.

Notes:
- In this codebase/toolchain, `luaL_error(...)` may be seen as returning `void`.
  In Lua C callbacks that return `int`, use:
  `luaL_error(state, "..."); return 0;`
- Do not mix contradictory contracts for the same API in different call paths.

### 7.2 Stack Balance

Every C function must leave the stack exactly as Lua expects:

- Each value returned to Lua is left on the top.
- All temporaries are popped.
- `lua_settop` or carefully tracked `lua_pop` calls must account for every
  intermediate push.

When using `lua_pcall` inside a binding (e.g. event dispatch), the traceback
function must be pushed below the callable so that its stack index remains stable
after the call frame is created.

For internal call paths that invoke Lua from C++ (phase hooks, listeners, slot
runtime rebuild), stack invariants are mandatory. Use explicit stack-top guards
(`CHECK_F` on entry/exit top equality) to fail fast on imbalance.

### 7.3 Protected Boundary Rules

Lua hard errors are non-local exits. Engine code must enforce these boundary
rules:

- All engine->Lua execution must run through protected calls (`lua_pcall` path).
- Never allow a Lua hard error to escape into unmanaged C++ call chains.
- In code paths that may raise `luaL_error`, avoid relying on post-call RAII
  cleanup of local objects.
- If a binding invokes Lua internally, traceback handler placement and cleanup
  must be deterministic on both success and failure.

### 7.4 Binding Author Checklist

Before merging a binding change, verify all items:

- Contract is explicit: hard error vs soft return is documented and tested.
- Wrong argument shapes/types are covered by negative-path tests (`pcall`).
- Missing-runtime-state behavior (entity missing/context absent) is tested.
- Stack top is balanced across all returns and all error paths.
- No temporary Lua stack objects are leaked (including traceback handlers).
- `CHECK_F` is used only for engine invariants, not user/script misuse.

---

## 8. Phase Safety and Mutation Rules

The `ScriptingModule` maps engine phases to event phase strings and invokes
scripts in specific phases. The full lifecycle per frame is:

| Engine phase | Event phase string | Scripts executed | Scene mutation allowed |
| --- | --- | --- | --- |
| `OnFrameStart` | `"frame_start"` | Event listeners only | No |
| `OnFixedSimulation` | `"fixed_simulation"` | `on_fixed_simulation` hook + event listeners | No |
| `OnGameplay` | `"gameplay"` | `RunSceneScripts` + event listeners | No |
| `OnSceneMutation` | `"scene_mutation"` | `RunSceneMutationScripts` + event listeners | **Yes** |
| `OnFrameEnd` | `"frame_end"` | Event listeners only | No |

Scene mutation (create/destroy/reparent nodes) is restricted to the
`scene_mutation` event phase. All mutating bindings check the current phase
before proceeding:

| Phase | Mutation allowed |
| --- | --- |
| `scene_mutation` | Yes |
| All other phases | No — returns `false`/`nil`, logs `WARNING` |

The check is performed via `GetActiveEventPhase(state)` (from `EventsBindings`).
The gate is implemented by the internal `IsMutationAllowedPhase` helper, which is
defined separately (but identically) in both `SceneBindings.cpp` and
`SceneNodeBindings.cpp`.

Operations that are currently phase-gated:

- `oxygen.scene.create_node`
- `oxygen.scene.destroy_node`
- `oxygen.scene.destroy_hierarchy`
- `oxygen.scene.reparent`
- `node:destroy`
- `node:set_parent`

Do not add scene mutators that bypass this check.

> This section defines the **scene mutation gate**. Other packs may apply
> stricter, domain-specific phase rules on top of this baseline. Example:
> physics distinguishes attach, command, and event-drain phases rather than
> using a single scene-style mutation gate.

---

## 9. Adding a New Binding

Follow these steps in order. Skipping lifecycle steps is the leading cause of
hard-to-debug memory leaks and GC crashes.

### Step 1 — Decide where it lives

Choose the target pack and namespace. If the feature does not fit an existing
namespace, create a new `BindingNamespace` entry in the pack's namespace array
and a corresponding `RegisterXBindings` function.

### Step 2 — Choose the representation

Apply the decision table from Section 4. If in doubt: does the object own any
heap memory or non-trivial C++ members? If yes, it needs tagged userdata with a
destructor.

### Step 3 — Implement registration

```text
Packs/<Pack>/
  XBindings.h       ← declaration of RegisterXBindings (and PushX/CheckX if any)
  XBindings.cpp     ← implementation
```

In `XBindings.cpp`:

1. Define the destructor (if non-trivial).
2. Implement `RegisterXMetatable` — create metatable, register dtor, register
   metamethods and methods.
3. Implement `RegisterXBindings` — push subtable, register public functions, pop.
4. Implement `PushX` and `CheckX` if the type is referred to from other files.

### Step 4 — Register with the pack

Add the new namespace to the pack's `constexpr std::array<BindingNamespace, N>`
in `XBindingPack.cpp`. Update the array size `N`.

### Step 5 — Validate context requirements

For each public function:

- Document which contexts it needs (`GetActiveFrameContext`, `GetActiveEngine`,
  binding context).
- Define explicit, tested behavior for each context-absent case (nil, false,
  or hard error per Section 7.1).

### Step 6 — Write tests

Add or extend a `Bindings_x_test.cpp` in `src/Oxygen/Scripting/Test` covering:

| Test category | What to verify |
| --- | --- |
| Surface exposure | Namespace exists; expected functions are callable |
| Happy path | Correct return values under valid conditions |
| Type/shape contract errors | `luaL_error` is raised for programmer misuse (validate via `pcall`) |
| Missing context | Returns nil/false or raises error per documented contract |
| Protected-boundary safety | Internal engine->Lua calls remain protected and stack-balanced |
| Lifecycle | Disconnect / unref / cleanup is safe to call multiple times |

---

## 10. Reference Files

### Core Plumbing

| File | Purpose |
| --- | --- |
| `BindingRegistry.h/.cpp` | `RegisterBindingNamespaces` — constructs the `oxygen` table hierarchy |
| `LuaBindingCommon.h` | Tag enum, `LuaBindingContext`, context push/get helpers, `PushOxygenSubtable` |
| `LuaBindingCommon.cpp` | All context implementations, `PushScriptParam`, `PushOxygenSubtable` body |
| `Contracts/IScriptBindingPack.h` | `IScriptBindingPack` interface, `ScriptBindingPackContext`, `ScriptBindingPackPtr` |

### Pack Entry Points

| File | Pack |
| --- | --- |
| `Packs/Core/CoreBindingPack.cpp` | Core pack entry point — registers 8 namespaces under `oxygen.*` (`app`, `conventions`, `events`, `hash`, `log`, `math`, `time`, `uuid`) |
| `Packs/Content/ContentBindingPack.cpp` | `oxygen.assets` |
| `Packs/Input/InputBindingPack.cpp` | `oxygen.input` |
| `Packs/Physics/PhysicsBindingPack.cpp` | `oxygen.physics.*` — 5 sub-namespaces via single dispatcher |
| `Packs/Scene/SceneBindingPack.cpp` | `oxygen.scene` |

### Canonical Examples

| Pattern | File |
| --- | --- |
| `lua_ref` lifecycle + event dispatch | `Packs/Core/EventsBindings.cpp` |
| Tagged userdata + shared layout (`AssetUserdata`) | `Packs/Content/ContentBindingsCommon.cpp`, `ContentUserdataBindings.cpp` |
| Metatable with property aliases (`__index`/`__newindex`) | `Packs/Scene/SceneNodeBindings.cpp` |
| Builder-style userdata with mutable scope state | `Packs/Scene/SceneQueryBindings.cpp` |
| Trivial POD userdata — untagged, no destructor | `Packs/Physics/PhysicsBindingsCommon.cpp` |
| Multi-sub-namespace pack with single dispatcher | `Packs/Physics/PhysicsBindingPack.cpp` |

### Tests

`src/Oxygen/Scripting/Test/Bindings_*_test.cpp` — one file per logical binding
group covering all test categories listed in Section 9.

---

## 11. Common Pitfalls

| Mistake | Consequence | Correct approach |
| --- | --- | --- |
| `__gc` metamethod on userdata | Silently ignored by Luau; every owned C++ object leaks | Use `lua_setuserdatadtor` with a tagged allocator |
| `lua_newuserdata` for non-trivial type | GC frees memory without calling C++ destructor | Use `lua_newuserdatatagged` + register dtor |
| `lua_touserdata` without tag check | Type confusion, bad cast, potential crash | Always verify `lua_userdatatag` before casting |
| Orphaned `lua_ref` | Lua values kept alive indefinitely, potential dangling callbacks | Unref on all exit paths; unref during shutdown |
| Mixed error semantics in one namespace | Inconsistent API surface for script authors | Pick one strategy (hard error or soft fallback) per namespace |
| Unbalanced stack | Subsequent API calls read wrong values; hard to diagnose | Count every push/pop; use `lua_gettop` assertions in debug builds |
| Reusing unstable relative stack indices after pushes/pops | Reads/writes wrong stack slot, latent corruption | Normalize/refresh indices before stack mutations; avoid stale cached indices |
| Unprotected engine->Lua call | Lua non-local exit crosses C++ frames; undefined behavior risk | Always execute engine->Lua via protected call wrappers |
| Relying on RAII after `luaL_error` path | Cleanup code may be skipped due to non-local exit | Do not place required cleanup after potential hard-error calls |
| Namespace registered but not in pack list (or vice versa) | Functions silently absent from `oxygen.*` | Always update both the pack array and the header |
| Scene mutator without phase gate | Mutations during rendering or read phases; non-deterministic crashes | Copy the `IsMutationAllowedPhase` guard pattern |
| Calling `RegisterXMetatable` after first `PushX` | `luaL_getmetatable` returns nil; userdata left without metatable | Register metatables at pack registration time, before any push |

---

## Practical Rule of Thumb

**Lifecycle correctness is not optional.** If a binding can retain ownership,
references, or heap-backed C++ state, treat it as a lifecycle-sensitive feature
first — and only then as a Lua API design problem.

Correct lifecycle + deterministic fallback behavior is the non-negotiable
baseline. API ergonomics come after.
