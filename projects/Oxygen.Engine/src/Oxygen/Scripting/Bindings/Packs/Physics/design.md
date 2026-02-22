# Oxygen Physics Lua Bindings Design

## 1. Purpose

Define the API shape, lifecycle, and implementation strategy for the
`oxygen.physics` Lua binding pack.

This design is grounded in:

- `src/Oxygen/Physics/System/I*.h` — backend-agnostic domain interfaces
- `src/Oxygen/Physics/Body/BodyDesc.h`, `Character/CharacterController.h`,
  `Query/Raycast.h`, `Query/Sweep.h`, `Query/Overlap.h`,
  `Events/PhysicsEvents.h`, `Handles.h`, `Shape.h`, `CollisionLayers.h`
- `src/Oxygen/PhysicsModule/PhysicsModule.h` and `ScenePhysics.h` — scene
  bridge and phase enforcement
- `src/Oxygen/PhysicsModule/PhaseContracts.md` — phase authority matrix
- `src/Oxygen/Scripting/Bindings/README.md` — binding architecture contracts

The overriding objective is to give game scripts practical physics power without
breaking Oxygen's phase authority, determinism, or ownership model.

---

## 2. Constraints From Oxygen Architecture

### 2.1 Phase Authority (non-negotiable)

PhysicsModule enforces three phases (see `PhaseContracts.md`):

| Phase | Allowed operations for scripts |
| --- | --- |
| `gameplay` | Attach/get bodies and characters; stage kinematic commands; queue force/impulse/velocity writes; character move |
| `fixed_simulation` | Script hooks (`on_fixed_simulation`) may execute, but no physics API calls are permitted; the physics solver owns exclusive world access |
| `scene_mutation` | Attach/get bodies and characters; read physics-authored state; drain events |
| `frame_start`, `frame_end` | Event listener dispatch only; no physics mutation |

Mutation bindings must gate on the active event phase (already provided by
`GetActiveEventPhase(state)` from `EventsBindings`). Read and query bindings
are permitted in any phase where a frame context and physics module are
available.

### 2.2 Motion Authority Matrix

Scripts must not contradict these authority rules:

| Body type | Authority | Script write access |
| --- | --- | --- |
| `static` | Scene-authored placement only | Read pose; no force/velocity writes |
| `kinematic` | Scene command authority | Write pose/velocity/force in `gameplay` |
| `dynamic` | Physics simulation authority | Force/impulse/velocity in `gameplay`; pose is read-only |
| Character | Command-authoritative (`CharacterFacade::Move`) | Provide movement intent in `gameplay` |

### 2.3 Scene Bridge Is the Primary Integration Surface

For scene-owned gameplay, the correct integration surface is
`ScenePhysics` (`PhysicsModule/ScenePhysics.h`):

```text
ScenePhysics::AttachRigidBody(physics_module, scene_node, body_desc)
ScenePhysics::GetRigidBody(physics_module, node_handle)
ScenePhysics::AttachCharacter(physics_module, scene_node, character_desc)
ScenePhysics::GetCharacter(physics_module, node_handle)
ScenePhysics::CastRay(physics_module, raycast_desc)
```

`ScenePhysics::CastRay` returns `SceneRayCastHit` — a hit that already
carries a `scene::NodeHandle` — making it the preferred query surface for
gameplay scripts.

For sweep and overlap queries, which are not in `ScenePhysics`, use
`PhysicsModule::Queries()` (which exposes `IQueryApi`) directly.

### 2.4 Accessing PhysicsModule From Bindings

The standard binding context access chain is:

```cpp
// Canonical pattern used by all bindings:
const auto engine = GetActiveEngine(state);
if (!engine) { return SoftFail; }

const auto physics_opt = engine->GetModule<PhysicsModule>();
if (!physics_opt) { return SoftFail; }
PhysicsModule& physics = physics_opt->get();
```

`AsyncEngine::GetModule<T>()` returns
`std::optional<std::reference_wrapper<T>>`. It returns an empty optional when
the module is not registered. The binding must unwrap it via `.get()` before
use. All binding functions that need physics must apply this pattern and
return `nil`/`false` on empty optional, consistent with soft-fallback policy.

### 2.5 Existing Binding Infrastructure

All rules from `src/Oxygen/Scripting/Bindings/README.md` apply in full:

- Non-trivial userdata: `lua_newuserdatatagged` + `lua_setuserdatadtor`.
- Never use `__gc` for Luau userdata; it is silently ignored.
- Ref hygiene: `lua_unref` on all failure paths and during shutdown.
- Context retrieval: `GetActiveEngine` / `GetActiveFrameContext` / `GetBindingContextFromScriptArg`.
- Stack discipline: every push balanced.
- Mutation gating: `GetActiveEventPhase` check before any write.

---

## 3. Design Goals

1. Provide practical gameplay scripting power for physics without exposing backend internals.
2. Keep scripts scene-centric: prefer node handles and scene nodes over raw body IDs.
3. Preserve deterministic phase behavior; scripts cannot bypass phase gates.
4. Distinguish read-access (all phases) from mutation access (`gameplay` and
   `scene_mutation` for attach; `gameplay` only for force/velocity/move).
5. Use typed userdata handles (not raw integers) to prevent ID-mix mistakes.
6. Mirror widely adopted scripting patterns from leading engines:
   - Scene/node-centric attach/get.
   - Fixed-step-aware command model for characters.
   - Stateless, call-once query helpers.
   - Event-drain API for contact/trigger callbacks.

---

## 4. Non-Goals (V1)

- Exposing aggregate, articulation, joint, vehicle, or soft-body domains.
- Solver/constraint tuning or world configuration from scripts.
- Multi-world or world lifecycle management from scripts.
- Raw backend object pointers or internal IDs as primary API.
- Shape resource management from scripts (shape is passed as a descriptor field).

---

## 5. Namespace Layout

One root with four submodules registered as `BindingNamespace` entries:

```text
oxygen.physics
├── oxygen.physics.body      — body attach/get, BodyHandle operations
├── oxygen.physics.character — character attach/get, CharacterHandle operations
├── oxygen.physics.query     — raycast, sweep, overlap
├── oxygen.physics.events    — drain physics events
└── oxygen.physics.constants — body_type, body_flags, event_type enums as strings
```

The root `oxygen.physics` table itself holds no public functions in V1; it acts
as a namespace container only. A debug `__index` guard should be added in
non-release builds following the same pattern as `oxygen.scene`.

---

## 6. API Reference (V1)

### 6.1 `oxygen.physics.body`

Body attach/get uses the same phase gate as rigid-body lifecycle operations
(see Section 8). Force/velocity writes on an already-attached handle require
`gameplay` only.

| Function | Phase required | Returns | Notes |
| --- | --- | --- | --- |
| `attach(node, desc)` | `gameplay` or `scene_mutation` | `BodyHandle \| nil` | Calls `ScenePhysics::AttachRigidBody`. Returns `nil` on failure. |
| `get(node)` | any | `BodyHandle \| nil` | Calls `ScenePhysics::GetRigidBody`. |

**`desc` table fields** (all optional, defaults shown):

| Field | C++ source | Default | Notes |
| --- | --- | --- | --- |
| `body_type` | `BodyDesc::type` | `"static"` | `"static"`, `"dynamic"`, `"kinematic"` |
| `flags` | `BodyDesc::flags` | `{"enable_gravity"}` | Array of flag strings (see Section 9.1) |
| `shape` | `BodyDesc::shape` | `{type="sphere", radius=0.5}` | See Section 9.3 |
| `mass_kg` | `BodyDesc::mass_kg` | `1.0` | |
| `linear_damping` | `BodyDesc::linear_damping` | `0.05` | |
| `angular_damping` | `BodyDesc::angular_damping` | `0.05` | |
| `gravity_factor` | `BodyDesc::gravity_factor` | `1.0` | |
| `friction` | `BodyDesc::friction` | `0.5` | |
| `restitution` | `BodyDesc::restitution` | `0.0` | |
| `collision_layer` | `BodyDesc::collision_layer` | `0` | Integer |
| `collision_mask` | `BodyDesc::collision_mask` | `0xFFFFFFFF` | Integer |

> **Transform semantics:** `ScenePhysics::AttachRigidBody` automatically
> samples the node's current world transform and injects it into
> `BodyDesc::initial_position` / `BodyDesc::initial_rotation` before
> calling `IBodyApi::CreateBody`. The Lua `desc` table does **not** expose
> `initial_position` or `initial_rotation` \u2014 move the node to the desired
> world position *before* calling `attach` if specific placement is required.

The same automatic node-transform injection applies to `character.attach` via
`ScenePhysics::AttachCharacter` and `CharacterDesc::initial_position` /
`CharacterDesc::initial_rotation`.

| Method | Phase required | Returns | C++ call |
| --- | --- | --- | --- |
| `:is_valid()` | any | `bool` | `IsValid(body_id_)` |
| `:get_id()` | any | `BodyId userdata` | — |
| `:get_body_type()` | any | `string` | via body mapping |
| `:get_position()` | any | `vec3 \| nil` | `IBodyApi::GetBodyPosition` |
| `:get_rotation()` | any | `quat \| nil` | `IBodyApi::GetBodyRotation` |
| `:get_linear_velocity()` | any | `vec3 \| nil` | `IBodyApi::GetLinearVelocity` |
| `:get_angular_velocity()` | any | `vec3 \| nil` | `IBodyApi::GetAngularVelocity` |
| `:set_linear_velocity(vec3)` | `gameplay` | `bool` | `IBodyApi::SetLinearVelocity` |
| `:set_angular_velocity(vec3)` | `gameplay` | `bool` | `IBodyApi::SetAngularVelocity` |
| `:add_force(vec3)` | `gameplay` | `bool` | `IBodyApi::AddForce` |
| `:add_impulse(vec3)` | `gameplay` | `bool` | `IBodyApi::AddImpulse` |
| `:add_torque(vec3)` | `gameplay` | `bool` | `IBodyApi::AddTorque` |
| `:move_kinematic(pos, rot, dt)` | `gameplay` | `bool` | `IBodyApi::MoveKinematic` |
| `:to_string()` | any | `string` | `__tostring` metamethod |

> `BodyHandle` stores `(world_id: WorldId, body_id: BodyId)`. Both are
> trivially destructible values, so the userdata struct is trivially
> destructible and does **not** need a tagged destructor in V1.

### 6.2 `oxygen.physics.character`

| Function | Phase required | Returns | Notes |
| --- | --- | --- | --- |
| `attach(node, desc)` | `gameplay` or `scene_mutation` | `CharacterHandle \| nil` | Calls `ScenePhysics::AttachCharacter`. Returns `nil` on failure. |
| `get(node)` | any | `CharacterHandle \| nil` | Calls `ScenePhysics::GetCharacter`. |

**`desc` table fields:**

| Field | C++ source | Default | Notes |
| --- | --- | --- | --- |
| `shape` | `CharacterDesc::shape` | `{type="capsule", radius=0.5, half_height=1.0}` | See Section 9.3 |
| `mass_kg` | `CharacterDesc::mass_kg` | `80.0` | |
| `max_slope_angle` | `CharacterDesc::max_slope_angle_radians` | `0.7854` | Radians; ~45° |
| `max_strength` | `CharacterDesc::max_strength` | `100.0` | |
| `character_padding` | `CharacterDesc::character_padding` | `0.02` | |
| `penetration_recovery_speed` | `CharacterDesc::penetration_recovery_speed` | `1.0` | |
| `predictive_contact_distance` | `CharacterDesc::predictive_contact_distance` | `0.1` | |
| `collision_layer` | `CharacterDesc::collision_layer` | `0` | Integer |
| `collision_mask` | `CharacterDesc::collision_mask` | `0xFFFFFFFF` | Integer |

**`CharacterHandle` methods:**

| Method | Phase required | Returns | C++ call |
| --- | --- | --- | --- |
| `:is_valid()` | any | `bool` | `IsValid(character_id_)` |
| `:get_id()` | any | `CharacterId userdata` | — |
| `:move(velocity, jump, dt)` | `gameplay` | `MoveResult table \| nil` | `CharacterFacade::Move` |
| `:to_string()` | any | `string` | `__tostring` |

**`move` input:** `velocity` is a `vec3` (desired velocity in world space);
`jump` is an optional `bool` (default `false`); `dt` is the delta time in
seconds (obtain from `oxygen.time.delta_seconds()`).

**`MoveResult` table fields** (from `CharacterMoveResult`):

| Field | C++ source | Type |
| --- | --- | --- |
| `is_grounded` | `CharacterState::is_grounded` | `bool` |
| `position` | `CharacterState::position` | `vec3` |
| `rotation` | `CharacterState::rotation` | `quat` |
| `velocity` | `CharacterState::velocity` | `vec3` |
| `hit_body` | `CharacterMoveResult::hit_body` | `BodyId userdata \| nil` |

### 6.3 `oxygen.physics.query`

All query functions are read-only and allowed in any phase where physics is available.

| Function | Returns | C++ call |
| --- | --- | --- |
| `raycast(desc)` | `RaycastHit table \| nil` | `ScenePhysics::CastRay` (preferred) then `IQueryApi::Raycast` |
| `sweep(desc, max_hits?)` | `SweepHit table[]` | `IQueryApi::Sweep` |
| `overlap(desc, max_hits?)` | `integer (count), BodyId[]` | `IQueryApi::Overlap` — user_data reinterpreted as `BodyId` |

> **`overlap` return contract:** `IQueryApi::Overlap` fills a
> `std::span<uint64_t> out_user_data`. `JoltBodies::CreateBody` now
> stores `body_id.get()` in the Jolt body's user_data slot immediately
> after creation, so each `uint64_t` value is the underlying `uint32_t`
> of a `BodyId`. The Lua binding reconstructs `BodyId` userdata via
> `BodyId { static_cast<uint32_t>(user_data) }`. This contract holds for
> all bodies registered through `ScenePhysics`/`PhysicsModule`.
>
> **Implementation note:** The binding calls `IQueryApi::Raycast` directly.
> `ScenePhysics::CastRay` cannot be used here: `SceneRayCastHit` carries no
> `body_id`, and `GetBodyIdForNode(NodeHandle)` is tag-gated (private to the
> `ScenePhysics` boundary). Instead, the `body_id` comes from the raw
> `RaycastHit`, and the `node` field is resolved via the public
> `GetNodeForBodyId(BodyId)` — yielding the same Tier A result: `node` is a
> live `SceneNode` userdata for scene-mapped bodies, `nil` for aggregate-owned
> or non-mapped bodies.

**`raycast` descriptor fields (`RaycastDesc`):**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `origin` | `vec3` | `{0,0,0}` | Required in practice |
| `direction` | `vec3` | `space::move::Forward` = `{0,-1,0}` | Must be normalized; engine is -Y forward |
| `max_distance` | `number` | `1000.0` | |
| `collision_mask` | `integer` | `0xFFFFFFFF` | |
| `ignore_bodies` | `BodyId[] \| nil` | `nil` | Optional list |

**`RaycastHit` result table:**

| Field | C++ source | Type | Notes |
| --- | --- | --- | --- |
| `body_id` | `RaycastHit::body_id` | `BodyId userdata` | Always present |
| `position` | `RaycastHit::position` | `vec3` | World-space hit point |
| `normal` | `RaycastHit::normal` | `vec3` | World-space surface normal |
| `distance` | `RaycastHit::distance` | `number` | Along ray direction |
| `node` | `SceneRayCastHit::node` | `SceneNode userdata \| nil` | Set when scene mapping found |

**`sweep` descriptor fields (`SweepDesc`):**

| Field | Type | Default |
| --- | --- | --- |
| `shape` | shape table | `{type="sphere", radius=0.5}` |
| `origin` | `vec3` | `{0,0,0}` |
| `direction` | `vec3` | engine forward |
| `max_distance` | `number` | `1000.0` |
| `collision_mask` | `integer` | `0xFFFFFFFF` |
| `ignore_bodies` | `BodyId[] \| nil` | `nil` |

**`SweepHit` result table fields:** `body_id`, `position`, `normal`, `distance`
(same layout as `RaycastHit`, no `node` field).

**`overlap` descriptor fields (`OverlapDesc`):**

| Field | Type | Default |
| --- | --- | --- |
| `shape` | shape table | `{type="sphere", radius=0.5}` |
| `center` | `vec3` | `{0,0,0}` |
| `collision_mask` | `integer` | `0xFFFFFFFF` |
| `ignore_bodies` | `BodyId[] \| nil` | `nil` |

`overlap` returns `count, body_id_array` (two return values) where each
element is a `BodyId` userdata reconstructed from the Jolt body's user_data
slot. `nil, nil` on failure.

### 6.4 `oxygen.physics.events`

The event API is **read-only** and restricted to the `scene_mutation` event
phase. This is not a policy choice — it is a sequencing fact:

- During `OnSceneMutation`, `PhysicsModule` runs **before** `ScriptingModule`
  (module priority order).
- `PhysicsModule::OnSceneMutation` calls `DrainPhysicsEvents()` internally,
  which drains the backend `IEventApi` queue and maps each event to
  `ScenePhysicsEvent` — a struct that carries already-resolved
  `optional<scene::NodeHandle>` values for both bodies.
- By the time `ScriptingModule` dispatches `scene_mutation` listeners, the
  backend queue is empty. Scripts must consume mapped events via
  `PhysicsModule::ConsumeSceneEvents()`, not via `IEventApi::DrainEvents`.
- During `gameplay`, physics has not yet stepped for this frame — there are no
  new events to read. During `frame_start`/`frame_end`/`fixed_simulation`,
  the PhysicsModule has not yet reconciled events.

There is no `pending_count` API. `ConsumeSceneEvents()` performs a single
atomic `swap` — it returns all queued events and clears the source in one
operation. Checking before consuming is unnecessary; an empty result table
is the unambiguous signal.

| Function | Phase required | Returns | Notes |
| --- | --- | --- | --- |
| `drain()` | `scene_mutation` | `PhysicsEvent table[]` | Calls `PhysicsModule::ConsumeSceneEvents()`. Returns `{}` on wrong phase or no events. |

**`PhysicsEvent` table fields** (from `PhysicsModule::ScenePhysicsEvent`):

| Field | C++ source | Type | Notes |
| --- | --- | --- | --- |
| `type` | `raw_event.type` | `string` | `"contact_begin"`, `"contact_end"`, `"trigger_begin"`, `"trigger_end"` |
| `node_a` | `ScenePhysicsEvent::node_a` | `SceneNode userdata \| nil` | Pre-resolved by PhysicsModule; `nil` if body has no scene mapping |
| `node_b` | `ScenePhysicsEvent::node_b` | `SceneNode userdata \| nil` | Pre-resolved by PhysicsModule |
| `body_a` | `raw_event.body_a` | `BodyId userdata` | Always present |
| `body_b` | `raw_event.body_b` | `BodyId userdata` | Always present |
| `contact_normal` | `raw_event.contact_normal` | `vec3` | Zero vector for trigger events |
| `contact_position` | `raw_event.contact_position` | `vec3` | Zero vector for trigger events |
| `penetration_depth` | `raw_event.penetration_depth` | `number` | Zero for trigger events |
| `applied_impulse` | `raw_event.applied_impulse` | `vec3` | Zero for trigger/end events |

> Unlike the previous design draft, `node_a`/`node_b` do **not** require a
> `GetNodeForBodyId` lookup in the binding — they are already resolved in
> `ScenePhysicsEvent` by the time the binding runs.

### 6.5 `oxygen.physics.constants`

A frozen read-only table (no `__newindex`). Values:

```lua
oxygen.physics.constants.body_type = {
    static    = "static",
    dynamic   = "dynamic",
    kinematic = "kinematic",
}

oxygen.physics.constants.body_flags = {
    none                          = "none",
    enable_gravity                = "enable_gravity",
    is_trigger                    = "is_trigger",
    enable_ccd                    = "enable_ccd",  -- continuous collision detection
}

oxygen.physics.constants.event_type = {
    contact_begin  = "contact_begin",
    contact_end    = "contact_end",
    trigger_begin  = "trigger_begin",
    trigger_end    = "trigger_end",
}
```

---

## 7. Userdata Plan

### 7.1 Userdata Tags — Not Allocated

All four physics userdata types are **trivially destructible** POD structs
(they hold only `NamedType<uint32_t>` fields). Per the No-Leak Policy in
`README.md`, trivially destructible payloads may use **untagged
`lua_newuserdata`** — no tag integer is reserved or required. Tags are only
allocated for types needing a Luau GC destructor callback, which these do not.

Do not extend `LuauUserdataTag` in `LuaBindingCommon.h` for these types.

### 7.2 Userdata Layouts

All four types are **trivially destructible** (they store only `uint32_t`
`NamedType` values). They use `lua_newuserdata` (untagged), not
`lua_newuserdatatagged`, and do **not** require `lua_setuserdatadtor`.

> This is allowed by the No-Leak Policy: trivially destructible payloads
> may use untagged userdata. The Luau GC freeing raw memory is safe for POD.

```cpp
// PhysicsBodyHandleUserdata — stores handle compound (world + body)
struct PhysicsBodyHandleUserdata {
    physics::WorldId   world_id;
    physics::BodyId    body_id;
    // body_type cached to gate velocity/force writes without a round-trip:
    physics::body::BodyType body_type;
};

struct PhysicsCharacterHandleUserdata {
    physics::WorldId     world_id;
    physics::CharacterId character_id;
};

struct PhysicsBodyIdUserdata {
    physics::BodyId body_id;
};

struct PhysicsCharacterIdUserdata {
    physics::CharacterId character_id;
};
```

### 7.3 Metatable Names

| Metatable constant | String value |
| --- | --- |
| `kPhysicsBodyHandleMetatable` | `"oxygen.physics.body_handle"` |
| `kPhysicsCharacterHandleMetatable` | `"oxygen.physics.character_handle"` |
| `kPhysicsBodyIdMetatable` | `"oxygen.physics.body_id"` |
| `kPhysicsCharacterIdMetatable` | `"oxygen.physics.character_id"` |

### 7.4 Standard Helpers Per Type

Each type exposes `PushX` and `CheckX` in the shared
`PhysicsBindingsCommon.h`:

```cpp
auto PushBodyHandle(lua_State*, WorldId, BodyId, body::BodyType) -> int;
auto CheckBodyHandle(lua_State*, int index) -> PhysicsBodyHandleUserdata*;

auto PushCharacterHandle(lua_State*, WorldId, CharacterId) -> int;
auto CheckCharacterHandle(lua_State*, int index) -> PhysicsCharacterHandleUserdata*;

auto PushBodyId(lua_State*, BodyId) -> int;
auto CheckBodyId(lua_State*, int index) -> PhysicsBodyIdUserdata*;

auto PushCharacterId(lua_State*, CharacterId) -> int;
auto CheckCharacterId(lua_State*, int index) -> PhysicsCharacterIdUserdata*;
```

All four types share `is_valid()` and `__tostring` metamethods.

**`__eq` semantics differ by type:**

- **`PhysicsBodyHandleUserdata`**: `__eq` compares `(world_id, body_id)` as
  a pair. Comparing only `body_id` would produce false positives in a
  multi-world scenario (a future V2 feature) where the same integer can be
  reused in different worlds.
- **`PhysicsCharacterHandleUserdata`**: `__eq` compares `(world_id, character_id)` as a pair, for the same reason.
- **`PhysicsBodyIdUserdata`**, **`PhysicsCharacterIdUserdata`**: `__eq`
  compares the single raw `uint32_t` value. These bare ID types have no
  world context, so raw value equality is the only meaningful comparison.

---

## 8. Mutation Gating Policy

There are two distinct gate levels for physics mutations:

- **Attach gate** (`gameplay` or `scene_mutation`): matches the explicit
  C++ contract in `ScenePhysics::AttachRigidBody` and `ScenePhysics::AttachCharacter`
  which allow both `kGameplay` and `kSceneMutation`.
- **Command gate** (`gameplay` only): force, impulse, torque, velocity writes,
  and character move are staging operations for the upcoming fixed simulation
  step and must not occur during `kSceneMutation` (which is the pull/reconcile
  phase, not the push/command phase).

```cpp
auto IsAttachAllowed(lua_State* state) -> bool {
    const auto phase = GetActiveEventPhase(state);
    return phase == "gameplay" || phase == "scene_mutation";
}

auto IsCommandAllowed(lua_State* state) -> bool {
    const auto phase = GetActiveEventPhase(state);
    return phase == "gameplay";
}

auto IsEventDrainAllowed(lua_State* state) -> bool {
    const auto phase = GetActiveEventPhase(state);
    return phase == "scene_mutation";
}
```

Policy table:

| Operation | Phase gate | On rejection |
| --- | --- | --- |
| `body.attach` | `IsAttachAllowed` | Return `nil`, log `WARNING` |
| `character.attach` | `IsAttachAllowed` | Return `nil`, log `WARNING` |
| `body:set_linear_velocity` etc. | `IsCommandAllowed` | Return `false`, log `WARNING` |
| `body:add_force`, `add_impulse`, `add_torque` | `IsCommandAllowed` | Return `false`, log `WARNING` |
| `body:move_kinematic` | `IsCommandAllowed` | Return `false`, log `WARNING` |
| `character:move` | `IsCommandAllowed` | Return `nil`, log `WARNING` |
| `events.drain` | `IsEventDrainAllowed` | Return `{}`, no warning (empty is expected in non-mutation phases) |
| `query.*` | none (read-only) | Return `nil` if engine unavailable |

---

## 9. Data Mapping Rules

### 9.1 Enum Strings

| C++ enum | Lua string |
| --- | --- |
| `BodyType::kStatic` | `"static"` |
| `BodyType::kDynamic` | `"dynamic"` |
| `BodyType::kKinematic` | `"kinematic"` |
| `BodyFlags::kNone` | `"none"` |
| `BodyFlags::kEnableGravity` | `"enable_gravity"` |
| `BodyFlags::kIsTrigger` | `"is_trigger"` |
| `BodyFlags::kEnableContinuousCollisionDetection` | `"enable_ccd"` |
| `PhysicsEventType::kContactBegin` | `"contact_begin"` |
| `PhysicsEventType::kContactEnd` | `"contact_end"` |
| `PhysicsEventType::kTriggerBegin` | `"trigger_begin"` |
| `PhysicsEventType::kTriggerEnd` | `"trigger_end"` |

`body_flags` in the `desc` table is accepted as an **array of strings**. The
implementation OR-combines all flag bits. Unknown strings produce `luaL_error`.

### 9.2 Shape Descriptor Tables

`CollisionShape` is a `std::variant<SphereShape, BoxShape, CapsuleShape, MeshShape>`.
Lua representation:

```lua
-- Sphere
{ type = "sphere", radius = 0.5 }

-- Box (extents are half-extents, matching engine convention)
{ type = "box", extents = vector(0.5, 0.5, 0.5) }

-- Capsule
{ type = "capsule", radius = 0.5, half_height = 1.0 }

-- Mesh (references a loaded geometry asset by GUID string)
{ type = "mesh", geometry_guid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" }
```

Mesh shape requires that the asset is already loaded and cached via
`oxygen.assets`; the binding resolves the GUID to a `GeometryAsset`
`shared_ptr` via the asset runtime. If the asset is not found, `luaL_error`
is raised.

### 9.3 Numerics

- All float fields are validated as finite (`std::isfinite`). Non-finite
  values produce `luaL_argerror`.
- Angular values (`max_slope_angle`) are in radians, matching the C++ struct.

### 9.4 Vector and Quaternion Mapping

- `vec3` uses the Luau native vector primitive (`lua_pushvector` / `lua_isvector`),
  matching the convention used by Scene bindings (`CheckVec3` / `PushVec3`).
- `quat` uses the `QuatUserdata` type and `CheckQuat` / `PushQuat` helpers from
  `SceneNodeBindings.h`.

---

## 10. Error and Fallback Semantics

| Condition | Behavior |
| --- | --- |
| No active engine (`GetActiveEngine` returns null) | Return `nil` / `false`; no hard error |
| No active frame context | Return `nil` / `false`; no hard error |
| `PhysicsModule` not registered | Return `nil` / `false`; no hard error |
| Wrong Lua argument type | `luaL_check*` / `luaL_argerror` (hard error) |
| Wrong phase for mutation | Return `nil` / `false`; log `WARNING` with phase name |
| Backend `PhysicsResult` failure | Return `nil` / `false`; optionally log `WARNING` with `to_string(error)` |
| Invalid ID (body/character not found) | Return `nil`; log `WARNING` |

All soft-fallback error paths must behave deterministically: the same input
in the wrong context always produces the same output value, with no side
effects.

---

## 11. File and Pack Structure

```text
Packs/Physics/
  PhysicsBindingPack.h            — CreatePhysicsBindingPack()
  PhysicsBindingPack.cpp          — pack class + kPhysicsNamespaces array
  PhysicsBindingsCommon.h         — shared userdata types, Push*/Check* helpers
  PhysicsBindingsCommon.cpp       — metatable registrations, common helpers
  PhysicsBodyBindings.h           — RegisterBodyBindings()
  PhysicsBodyBindings.cpp
  PhysicsCharacterBindings.h      — RegisterCharacterBindings()
  PhysicsCharacterBindings.cpp
  PhysicsQueryBindings.h          — RegisterQueryBindings()
  PhysicsQueryBindings.cpp
  PhysicsEventsBindings.h         — RegisterPhysicsEventsBindings()
  PhysicsEventsBindings.cpp
  PhysicsConstantsBindings.h      — RegisterPhysicsConstantsBindings()
  PhysicsConstantsBindings.cpp
```

`PhysicsBindingPack.cpp` declares a **single top-level namespace entry**,
following the convention used by `SceneBindingPack` and `CoreBindingPack`:

```cpp
constexpr std::array<BindingNamespace, 1> kPhysicsNamespaces = {{
    { .name = "physics", .register_fn = RegisterPhysicsBindings },
}};
```

`RegisterPhysicsBindings` is a single dispatcher function that sequentially
calls all sub-registrars under `oxygen.physics`:

```cpp
auto RegisterPhysicsBindings(lua_State* state, int oxygen_idx) -> void {
    RegisterBodyBindings(state, oxygen_idx);
    RegisterCharacterBindings(state, oxygen_idx);
    RegisterQueryBindings(state, oxygen_idx);
    RegisterPhysicsEventsBindings(state, oxygen_idx);
    RegisterPhysicsConstantsBindings(state, oxygen_idx);
}
```

Each sub-registrar creates the `oxygen.physics.<sub>` sub-table internally
nested under `oxygen_idx`. This matches the established single-name
convention and avoids dotted `__namespaces` keys.

The pack must be added to the default engine binding pack list (wherever
`CreateCoreBindingPack`, `CreateSceneBindingPack`, etc. are registered).

---

## 12. Registration Order

Inside `RegisterBodyBindings` (and similarly for others), metatables must be
registered before the module table is pushed:

```text
RegisterPhysicsBodyHandleMetatable(state)   ← includes userdata dtors if any
RegisterPhysicsBodyIdMetatable(state)
PushOxygenSubtable(state, oxygen_idx, "physics")  ← ensure oxygen.physics exists
PushOxygenSubtable(state, physics_idx, "body")    ← oxygen.physics.body
lua_pushcfunction + lua_setfield for attach, get
lua_pop(state, 1)  ← body
lua_pop(state, 1)  ← physics
```

> `PushOxygenSubtable` is idempotent: it reuses an existing table if
> `oxygen.physics` was already created by a previously registered namespace.

---

## 13. Implementation Plan

### V1 — Complete Implementation

All functionality listed in Section 6 is implemented in V1. This includes
sweep and overlap queries, the full `BodyHandle` and `CharacterHandle` method
sets, the events `drain`, and the constants table.

1. Add `Packs/Physics/` directory and CMake target.
2. Implement `PhysicsBindingsCommon`:
   - All four userdata structs and metatables.
   - `PushBodyHandle`, `CheckBodyHandle`, `PushCharacterHandle`, `CheckCharacterHandle`.
   - `PushBodyId`, `CheckBodyId`, `PushCharacterId`, `CheckCharacterId`.
   - Helper `GetPhysicsModule(state) -> observer_ptr<PhysicsModule>`.
   - `IsAttachAllowed`, `IsCommandAllowed`, `IsEventDrainAllowed` phase helpers.
3. Implement `PhysicsBodyBindings`:
   - `body.attach`, `body.get`.
   - Full `BodyHandle` method set: `is_valid`, `get_id`, `get_body_type`,
     `get_position`, `get_rotation`, `get_linear_velocity`, `get_angular_velocity`,
     `set_linear_velocity`, `set_angular_velocity`, `add_force`, `add_impulse`,
     `add_torque`, `move_kinematic`.
4. Implement `PhysicsCharacterBindings`:
   - `character.attach`, `character.get`.
   - `CharacterHandle` methods: `is_valid`, `get_id`, `move`.
5. Implement `PhysicsQueryBindings`:
   - `query.raycast` (via `ScenePhysics::CastRay` → fallback to `IQueryApi::Raycast`).
   - `query.sweep` (via `IQueryApi::Sweep` with `span<SweepHit>` output).
   - `query.overlap` (via `IQueryApi::Overlap` with `span<uint64_t>` output, returns `count, BodyId[]`).
6. Implement `PhysicsEventsBindings`:
   - `events.drain` via `PhysicsModule::ConsumeSceneEvents()`.
7. Implement `PhysicsConstantsBindings`:
   - Frozen constants table.
8. Wire `PhysicsBindingPack` and register with engine.
9. Add tests (see Section 14).

### V2 — Extension Round

Features deferred to V2 because they involve cross-subsystem coupling
or genuinely new domain design work, not implementation complexity:

- **`oxygen.events` physics bridge**: forward drained events into the
  `oxygen.events` queue under reserved event names (`physics.contact_begin`,
  `physics.contact_end`, etc.). Requires coordinating with `EventsBindings`
  dispatch machinery; not risky but adds a coupling not needed for V1 utility.
- **Aggregate/articulation/vehicle/soft-body scripting surface**: these are
  separate domains with distinct APIs requiring dedicated design sections
  before implementation.

---

## 14. Testing Strategy

Add `src/Oxygen/Scripting/Test/Bindings_physics_test.cpp` covering:

| Category | What to verify |
| --- | --- |
| Surface exposure | `oxygen.physics.body`, `.character`, `.query`, `.events`, `.constants` exist and are tables |
| Function presence | `attach`, `get`, `raycast`, `sweep`, `overlap`, `drain` are callable |
| Type errors | Wrong argument types produce `luaL_error` |
| Missing engine | All functions return `nil`/`false`/`0`/`{}` when `GetActiveEngine` returns null |
| Phase gate — attach | `body.attach` and `character.attach` return `nil` outside `gameplay`/`scene_mutation` |
| Phase gate — command | Force/velocity writes on `BodyHandle` return `false` outside `gameplay` |
| Phase gate — events | `events.drain()` returns `{}` outside `scene_mutation` |
| Overlap results | `overlap()` returns integer count + `BodyId` userdata array (reconstructed from Jolt user_data) |
| Happy path — body | Attach returns a valid `BodyHandle`; `BodyHandle:is_valid()` returns `true` |
| Happy path — character | Attach returns valid `CharacterHandle`; `move` returns a result table |
| Happy path — raycast | Returns `nil` on no hit; returns table with `body_id`, `position`, `normal`, `distance` on hit |
| Happy path — drain | Returns empty table when no events queued; returns correct event shape |
| Userdata lifecycle | Constructing and collecting `BodyHandle` userdata produces no leaks |
| Constants table | Values match C++ enum strings; write attempt raises error |

---

## 15. Acceptance Criteria For Starting Implementation

- [ ] `PhysicsBindingPack` compiles and registers with no linker errors.
- [ ] `oxygen.physics.body`, `oxygen.physics.character`, `oxygen.physics.query`,
  `oxygen.physics.events`, `oxygen.physics.constants` are present in the
  runtime environment.
- [ ] Full V1 API surface implemented and all tests in Section 14 pass.
- [ ] No lifecycle leaks: all userdata confirmed trivially destructible (no
  `__gc`, confirmed no non-trivial members).
- [ ] No phase-contract violations: mutating APIs gated, read APIs always safe.
- [ ] Code passes clang-tidy and follows all conventions in `README.md`.
