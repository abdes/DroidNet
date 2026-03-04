# Physics Data Architecture

## Mental Model

### Foundational Conventions

Four concerns are cross-cutting: they apply at every layer and every
subsystem. They are defined here once and never re-litigated.

#### 1 — Asset Identity

Every authored asset has a single, permanent identity expressed as its
*canonical virtual path* — a source-rooted, slash-delimited UTF-8 string.
The virtual path is the source of truth for humans: it is what authors write,
what version-control tracks, and what tools display. The `VirtualPathResolver`
maps virtual paths to `AssetKey` values by consulting all mounted sources in
priority order; it is the single gateway between the human-readable path world
and the machine key world.

**Structure.** A canonical virtual path has the following fixed anatomy:

```text
/<MountRoot>[/<MountRoot2>]/<Domain>/<Category>/[<Subcategory>/]<AssetName>.<TypeSuffix>
```

More precisely, the first one or two segments form the **mount root** — the
identifier of the source that owns this asset. The remaining segments are the
**asset path within the source**. The full string (mount root + asset path) is
what is hashed to produce the `AssetKey`; the mount root is therefore part of
the identity, not just a routing hint.

**Syntax.** A canonical virtual path must conform to all of the following
rules. Violation of any rule makes the string non-canonical and the behaviour
of all consuming systems undefined.

- Encoded as **UTF-8**.
- Always **absolute**: begins with exactly one `/` character.
- **Segments** are the non-empty substrings between consecutive `/`
  separators. Empty segments (`//`) and a trailing `/` are prohibited.
- The **only permitted characters** within any segment are: ASCII letters
  (A–Z, a–z), ASCII digits (0–9), hyphens (`-`), underscores (`_`), and
  dots (`.`). Spaces, control characters, and all other Unicode code points
  are prohibited including in the leaf segment.
- Dots are restricted to the **leaf segment only**, where exactly one dot
  separates the asset name from its type suffix. Non-leaf segments must
  not contain dots.
- The segments `.` and `..` are prohibited — no relative traversal.
- Total byte length must not exceed **512 bytes** (excluding any null
  terminator).
- **Case-sensitive** throughout, including the mount root. All tools must
  preserve and compare casing exactly; platform case-folding must never
  be applied silently.

**Mount roots.** A mount root is a short identifier registered with the
`VirtualPathResolver` that maps to a physical content source. Oxygen defines
the following standard mount roots:

| Mount root | Source type | Example |
| --- | --- | --- |
| `/Engine` | Built-in engine assets (shipped with the engine binary, read-only) | `/Engine/Physics/Materials/Default.opmat` |
| `/Game` | Project-specific game assets (the primary authoring namespace) | `/Game/Physics/Materials/Rubber.opmat` |
| `/.cooked` | Active loose cooked output root (`container.index.bin`); used by the editor and cooker tools during development | `/.cooked/Physics/Materials/Rubber.opmat` |
| `/Pak/<name>` | Named PAK file mount (production and DLC); `<name>` is the PAK's declared mount identifier | `/Pak/DLC01/Game/Physics/Materials/Lava.opmat` |

Resolution priority: patch PAKs beat base PAKs, base PAKs beat loose cooked
roots, loose cooked roots beat engine built-ins. When multiple sources map
the same virtual path to different `AssetKey` values, the highest-priority
mapping wins; conflicts are logged. A tombstone entry in a patch manifest
masks lower-priority mappings for the same path.

Because the mount root is part of the identity, `/Engine/Physics/Materials/Default.opmat`
and `/Game/Physics/Materials/Default.opmat` are **different assets** with
different `AssetKey` values, even if their content is identical.

**Semantics.**

- The path is **virtual**: it has no mandatory correspondence to the physical
  filesystem layout. A loose cooked root mounted at `/.cooked` stores
  `Rubber.opmat` under its configured `physics_materials_subdir`; the virtual
  path `/.cooked/Physics/Materials/Rubber.opmat` is derived from the layout's
  `virtual_mount_root` field, not from the filesystem path of the file.
- A rename of any part of the virtual path — mount root, domain, asset name,
  or type suffix — produces a **new identity**. Cooked artifacts referencing
  the old `AssetKey` become dangling. This is intentional: it requires
  explicit reference updates rather than silent aliasing.
- Two assets with identical content but different virtual paths have different
  identities. The `AssetKey` encodes the path, not the content.
- A virtual path **does not encode a version**. Versioning is handled
  exclusively by the integrity hash (Convention 3).

**Naming conventions.**

| Segment | Convention | Examples |
| --- | --- | --- |
| `MountRoot` | Defined by the mount registry; see mount-roots table above | `/Engine`, `/Game`, `/.cooked` |
| `Domain` | **PascalCase**, one word, broad functional area that owns the asset class | `Physics`, `Rendering`, `Audio`, `Animation`, `World`, `UI` |
| `Category` | **PascalCase**, one or two words, logical sub-grouping within the domain | `Materials`, `Shapes`, `Joints`, `Vehicles`, `SoftBodies`, `Characters`, `Scenes` |
| `Subcategory` | **PascalCase**, optional, for large categories | `Wheeled`, `Tracked`, `Ragdolls` |
| `AssetName` | **PascalCase**, descriptive noun or noun phrase unique within its category | `Rubber`, `BoulderHull`, `HingeDoor`, `PlayerCapsule` |
| `TypeSuffix` | **Oxygen descriptor extension** (lowercase, dot-prefixed); determines which descriptor reader handles the file — see table below | `.opmat`, `.ocshape`, `.opscene` |

The type suffix is the Oxygen cooked descriptor extension, not an arbitrary
convention. The following suffixes are defined by `LooseCookedLayout`:

| Type suffix | Asset class |
| --- | --- |
| `.opmat` | Physics material |
| `.ocshape` | Collision shape |
| `.opscene` | Physics scene sidecar |
| `.opres` | Physics backend resource blob |
| `.oscene` | Scene |
| `.ogeo` | Geometry |
| `.omat` | Render material |
| `.otex` | Texture |
| `.obuf` | Buffer |
| `.oscript` | Script |
| `.oiact` | Input action |
| `.oimap` | Input mapping context |

**Examples of valid canonical virtual paths:**

```text
/Game/Physics/Materials/Rubber.opmat
/Game/Physics/Materials/Ice.opmat
/Game/Physics/Shapes/BoulderConvexHull.ocshape
/Game/Physics/Vehicles/Wheeled/SportsCar.opscene
/Game/World/Scenes/Showcase.oscene
/Game/World/Scenes/Showcase.opscene
/Engine/Physics/Materials/Default.opmat
/.cooked/Physics/Materials/Rubber.opmat
```

**Examples of invalid canonical virtual paths and the violated rule:**

```text
/game/Physics/Materials/Rubber.opmat   (lowercase mount root — case violation)
Physics/Materials/Rubber.opmat         (no leading slash — not absolute)
/Game/Physics/Materials/Rubber.opmat/  (trailing slash — prohibited)
/Game/Physics//Materials/Rubber.opmat  (empty segment — double slash)
/Game/Physics/Materials/../Rubber.opmat (relative traversal — prohibited)
/Game/Physics/Materials/my rubber.opmat (space — prohibited character)
/Game/Physics.Materials/Rubber.opmat   (dot in non-leaf segment — prohibited)
/Game/Physics/Materials/Rubber         (no type suffix — ambiguous asset class)
```

> **Implementation status (2026-03-04):** Phase 1 identity migration is now in
> place. `AssetKey` is a dedicated 16-byte value type, deterministic minting is
> `AssetKey::FromVirtualPath(canonical_virtual_path)`, and rehydration is
> `AssetKey::FromBytes(16-byte payload)`. Random GUID identity generation
> (`GenerateAssetGuid()`), `AssetKeyPolicy` branches, and cooker free-function
> minting paths were removed. `VirtualPathResolver::ResolveAssetKey(...)`
> remains a lookup API (with precedence/tombstone behavior), not a standalone
> identity primitive.

The machine representation of an identity is the `AssetKey`: the **xxHash3-128
of the UTF-8 canonical virtual path**, stored as 16 bytes. This is
deterministic (the same path always produces the same key), requires no
persistent registry, and is human-recoverable (any system can re-derive the
key if it knows the path). A rename is a new identity by design — this is
not a limitation, it is the correct policy.

Random GUIDs are **not used** for asset identity. GUIDs require a mutable
registry to stay meaningful and produce opaque values that are hostile to
debugging and diffing. The deterministic-key approach, used by UE5
(`FPackageId` = hash of package name), Godot (`ResourceUID` seeded from path),
and Frostbite (hash of asset string name), provides all the stability benefits
without registry overhead.

Content hashes are **not used** as identities. A content hash changes when
the asset's content changes; treating it as identity causes every reference to
the asset to silently break on the next edit. See Convention 3 for the only
legitimate use of content hashes.

#### 2 — Cross-Artifact References

References between assets use different mechanisms at each layer, and these
must not be mixed across layers:

| Layer | Reference type | Lifecycle | Breaks when |
| --- | --- | --- | --- |
| **L1 Authoring** | Canonical virtual path (human-readable string) | Persistent across sessions | Asset renamed or moved |
| **L2 Cooked** | `AssetKey` (xxHash3-128 of virtual path) | Persistent — survives cook reruns | Virtual path changes (intentional — a rename is a new asset) |
| **L2 Within a sealed collection** | Positional index (`uint32_t` offset into a fixed array) | Valid only within that specific sealed, version-stamped collection | Cook order changes; partial re-cook; identity check mismatch |
| **L3 Runtime** | Generational handle (see Convention 4) | Session-scoped; never persisted | Session ends |

Positional indices are the most fragile reference type. They are permitted
**only within a single sealed, version-stamped collection** and must always be
accompanied by an identity check that hard-fails on mismatch. They must never
cross collection boundaries.

A sealed collection is only safe for positional indexing if it is
**fully and atomically regenerated** on every cook run that modifies its
scope. A collection that is incrementally patched — where only changed entries
are rewritten while others remain from a previous run — cannot guarantee
positional stability. A version counter that increments after a patch confirms
that the content changed; it does not confirm that entry order was preserved.

Positional indices are therefore confined to **per-asset artifacts**: the
structures that are produced in a single, atomic pass for one specific asset.
Whenever the asset's source changes, the entire artifact is re-cooked from
scratch — the cook model provides the guarantee for free. No special
enforcement is required beyond the identity check at load time.

**Container-level structures must not use positional ordinal indices.** In
development (Loose Cooking) mode, re-cooking one asset and regenerating the
entire container is not a viable workflow — it destroys iteration speed.
Container-level lookup structures must instead be keyed by `AssetKey`: a
stable, deterministic key that does not shift when other entries are added or
removed. This allows any entry to be added, updated, or deleted independently
without affecting any other entry, making incremental development cooks
correct by construction. The physics resource descriptor table and the
container asset catalog are both `AssetKey`-keyed structures.

The following table documents the per-asset sealed collections that correctly
use positional indices, and explains why each one is safe:

| # | Collection | What the index addresses | Why it is safe |
| --- | --- | --- | --- |
| 1 | **Scene node array** — the flat ordered list of all nodes within one cooked scene asset | Every physics binding record, constraint, and vehicle wheel record addresses a scene node by its ordinal position in this array | ✅ **By design** — a scene is an asset; every time any part of the scene source changes, the entire scene is re-cooked, producing a new node array from scratch |
| 2 | **Physics scene sidecar** — the complete per-scene mapping of node positions to physics component settings | Per-component-type sub-table offsets are positional within the sidecar blob; scene node indices in every binding record are positional within row 1 | ✅ **By design** — the sidecar is a per-scene asset paired 1:1 with its scene; any change to the scene or to any physics component in it triggers a full re-cook of the sidecar |
| 3 | **Vehicle wheel topology table** — the array of wheel-to-axle assignments embedded within the sidecar | Each vehicle binding claims a contiguous slice (start + count) in the shared wheel array; each element maps a wheel node to its axle and lateral side | ✅ **By design** — wheel topology lives inside the sidecar; when wheel configuration changes, the vehicle is re-cooked, which re-cooks the sidecar in its entirety |
| 4 | **Cooked shape blob** — the opaque binary produced by the physics backend's shape cooking API for one shape asset | Within the blob: child shape ordinals in a compound shape, vertex/triangle indices in a mesh shape, height-field row/column addresses, sub-shape ID hierarchies | ✅ **By design** — each shape is an independent asset cooked atomically in one pass; the backend provides no patch API |
| 5 | **Cooked soft-body topology blob** — the opaque binary encoding the particle constraint graph for one soft-body asset | Within the blob: particle ordinals in edge, volume, dihedral-bend, and tether constraints; pinned-particle list; per-particle material slot ordinal | ✅ **By design** — produced in one pass from the source mesh; any edit triggers a full recook |
| 6 | **Cooked constraint blob** — the opaque binary encoding the settings for one joint or constraint asset | Within the blob: per-axis parameter arrays (limits, motors, springs); where the constraint references sub-shapes of a compound body, those sub-shape IDs are positional within the companion shape blob (row 4) | ✅ **By design** — produced atomically per asset; if the companion shape blob is re-cooked and its sub-shape ordering changes, this blob must also be re-cooked — a dependency tracking obligation, not a patching problem |
| 7 | **Cooked vehicle settings blob** — the opaque binary encoding driveline and chassis parameters for one vehicle asset | Within the blob: torque-curve sample arrays, per-gear ratio arrays, per-axle differential and anti-roll-bar parameter arrays | ✅ **By design** — produced atomically per asset; axle or gear count changes require a full recook because the backend provides no partial-update API |

**Cross-collection boundary violations** — the following uses are always wrong
regardless of version matching:

- A scene node position held by sidecar X used as an index into the node
  array of a different scene — even if both scenes have the same node count.
- A wheel slice offset from one vehicle record used against the wheel array
  of a sidecar produced in a different cook run.
- Any internal topology index from a backend blob used against a blob
  produced in a different cook run — even if the source was unchanged,
  because no backend cooker guarantees bit-identical output across versions.

##### Versioning contracts

**Contract V-1 — Virtual path and `AssetKey` references carry no version and
require none.**

A virtual path resolves at load time to the *current* version of the
referenced asset — whatever is presently cooked and mounted. The path is an
eternal name; the binary content behind it varies across cook runs. This is
correct and intentional:

- In development, the author always sees the freshest cook with no
  invalidation ceremony.
- In production, the mounted PAK fixes the content for the entire release;
  versioning is moot because the content cannot change.
- Staleness degrades **gracefully**: if the asset is missing, the resolver
  returns "not found" — there is no silent corruption path.

An `AssetKey` carries the same semantics. It is derived from the virtual path
and identifies the asset, not any particular binary version of it. The
integrity hash (Convention 3) separately answers whether the loaded binary
matches the one a consumer expected.

**Contract V-2 — A positional index requires no generation stamp, provided it
is confined to within its regeneration unit.**

A *regeneration unit* is a set of artifacts always produced together in a
single atomic cook pass. Because all arrays within a unit are produced
together, no cook run can leave an index stale while the indexed collection
is updated — the staleness scenario is structurally impossible, not merely
guarded against. No co-located hash, no generation counter, and no runtime
validation ceremony are needed for within-unit indices.

The regeneration units in the physics pipeline are:

| Regeneration unit | Artifacts produced together | Within-unit positional indices |
| --- | --- | --- |
| **Scene package** | Scene asset + its paired physics sidecar (including the wheel topology table embedded in the sidecar) | Scene node indices in all sidecar binding records; wheel slice offsets within the sidecar |
| **Cooked shape asset** | One cooked shape blob | Child shape ordinals, vertex/triangle indices, height-field addresses, sub-shape IDs — all internal to the blob |
| **Cooked soft-body asset** | One cooked soft-body topology blob | Particle ordinals in edge, volume, bend, and tether constraints; pinned-particle list; per-particle material slot |
| **Cooked constraint asset** | One cooked constraint blob | Per-axis parameter arrays internal to the blob |
| **Cooked vehicle asset** | One cooked vehicle settings blob | Torque-curve, gear-ratio, axle parameter arrays internal to the blob |

**Contract V-3 — Any reference that crosses a regeneration unit boundary must
use `AssetKey`, never a positional index.**

When one artifact needs to reference an asset from a different regeneration
unit, the only permitted reference form is the `AssetKey` of the target
asset. The runtime resolves the key to the currently loaded artifact and
takes full responsibility for version compatibility through the integrity
check (Convention 3). There is no positional ordinal form of a cross-unit
reference.

Examples of correctly structured cross-unit references:

- A sidecar binding record references a shape asset via its `AssetKey`. The
  scene node index within the same binding record is a within-unit reference
  (the sidecar and its scene are one regeneration unit) and needs no key.
- A constraint blob references its two body-attachment scene nodes via the
  sidecar's within-unit scene node indices. If the constraint needs to filter
  contacts by sub-shape, that filtering is done at L3 runtime using live
  generational handles, not at cook time using positional blob-internal IDs.
- The container asset catalog and physics resource descriptor table are
  `AssetKey`-keyed maps, not positional arrays, which allows them to be
  updated incrementally without any index stability concern.

#### 3 — Integrity

A content hash is a fingerprint of a specific, sealed artifact. Its sole
purpose is to answer: *"Has this artifact been altered since it was
produced?"* It serves integrity verification. It is not an identity. It is
not a reference. It is never stored as the primary key of anything.

**The standardized integrity algorithm for all individual cooked artifacts in Oxygen is
SHA-256 (256-bit output, 32 bytes).** No other hash algorithm is used for asset
integrity purposes. Specifically:

- CRC variants (CRC32, CRC64) are **prohibited** for asset integrity. CRC is an
  error-detection code designed for transmission noise; it can be
  intentionally collided by anyone with modest compute and provides no
  tamper-evidence whatsoever. *(Note: The monolithic PAK container file itself
  still correctly utilizes CRC32 as a fast file-level checksum; this prohibition
  applies exclusively to the individual artifacts/assets inside the payload).*
- MD5 and SHA-1 are **prohibited** for integrity. Both are cryptographically
  broken.
- xxHash and MurmurHash variants are **not integrity hashes**. They are fast
  non-cryptographic hashes suitable for keying (Convention 1) and runtime
  look-up tables, but not for integrity because they offer no collision
  resistance against adversarial inputs.

SHA-256 runs at 300–500 MB/s on modern hardware without AES-NI assistance.
At cook time this overhead is entirely acceptable. SHA-256 must not be
computed on the critical rendering or simulation path at runtime.

#### 4 — Runtime Handles

Every live physics object — body, character, constraint, vehicle — is
referred to at L3 by an **opaque, session-scoped handle** exposed by Oxygen's
hydration layer. Raw backend pointers must not escape the L3 boundary.

The handle format is **not uniform** across object types or backends, because
the backends themselves control their object lifetimes and there is no
cross-backend abstraction that dictates a generational packed integer for all
types. The actual internal representations are:

| Object type | Jolt backend | PhysX backend |
  | --- | --- | --- |
  | **Rigid body / soft body** | `BodyID` — Jolt's own generational handle: 23-bit body index + 8-bit sequence number, owned and managed by `BodyInterface`. Staleness is detectable natively; no additional infrastructure needed. | `PxRigidActor*` — raw pointer. Session teardown destroys all objects simultaneously; mid-session staleness (body removed during gameplay) is the owning system's responsibility. |
  | **Character controller** | `CharacterVirtual*` or `Character*` — ref-counted pointer; no Jolt handle type. Ref-counting prevents dangling access. Sim-membership staleness is the owning system's responsibility. | `PxController*` — raw pointer; same responsibility model as PhysX rigid body. |
  | **Constraint / joint** | `RefConst<Constraint>` — ref-counted pointer; Jolt provides no constraint handle type. Ref-counting prevents dangling access; the owning system must not use the reference after removing the constraint from the simulation. | `PxJoint*` — raw pointer; owning system's responsibility. |
  | **Vehicle** | `VehicleConstraint*` — a `Constraint` subclass; same ref-counted pointer model as constraints. | `PxVehicle*` — raw pointer; owning system's responsibility. |

**Shapes are not live session objects.** A cooked shape is an asset,
identified at L2 by its `AssetKey` and shared across any number of bodies
that reference it. At L3, a shape is instantiated as a backend shape object
(Jolt `ShapeRef`, PhysX `PxShape*`) cached by `AssetKey`. Bodies reference
their shape through the body handle, not through a separate shape handle.

**The Oxygen L3 handle contract** — regardless of which backend representation
is used internally:

- A handle is **session-scoped**: minted at L3 hydration, discarded at scene
  unload. It is never serialised, written to disk, or used across sessions.
- **A handle is valid for exactly one physics session.** A physics session is
  created when a scene is hydrated and torn down when the scene is unloaded —
  including re-loads from cache, which always produce a new session with a
  full re-hydration. Session teardown destroys the entire backend physics
  world; all handles become invalid simultaneously. No cross-session staleness
  is possible by construction.
- The only remaining stale-handle concern is **mid-session object removal**
  (e.g., an entity destroyed during gameplay). For Jolt rigid bodies, the
  `BodyID` sequence number provides a free safety net: removing and later
  re-adding a body to the same slot produces a different sequence, so any
  holder of the old `BodyID` gets an explicit invalid result from
  `BodyInterface`. For all other types (constraints, characters, vehicles),
  the owning system is responsible: once it removes an object from the
  simulation, it must not use the associated handle again.
- The mapping between a scene node index (stable, cooked) and its runtime
  handle (ephemeral, session-local) is maintained by the hydrator and rebuilt
  from scratch on every scene load.
- A handle must never be used to identify an asset for loading or persistence.
  Asset identity is always `AssetKey`. Handles are for in-simulation access
  only.

---

### Three Layers

- **L1 — Authoring:** Editable, human-authored source truth. The designer or
  artist works entirely within this layer; nothing here is opaque.
  - **Collision shapes** in authoring space. Each shape carries:
    - *Type* — one of: Sphere, Capsule, Box, Cylinder, Cone, Convex Hull,
      Triangle Mesh, Height Field, Plane, World Boundary, or Compound (a
      hierarchy of the above).
    - *Analytic parameters* — radius, half-extents, half-height, normal and
      distance (plane), height-field resolution and height scale, or
      world-boundary limits, as appropriate for the type.
    - *Local transform* — position, orientation (quaternion), and uniform or
      per-axis scale relative to the owning scene node. This is a body-space
      offset, distinct from the scene node's world transform: at hydration the
      two compose (`world_shape = node_world × shape_local`) so the physics
      backend places the collision volume correctly. It is not redundant with
      the scene node transform — it exists to correct pivot mismatches between
      the visual mesh and the collision volume, and to position each child
      within a compound shape independently.
    - *Own collision layer* — a single category identifier (integer index or
      named enum value) declaring which layer this shape belongs to. An object
      belongs to exactly one layer; multi-layer membership is not supported by
      design, as it produces ambiguous collision semantics that the engine
      cannot resolve without author intent.
    - *Target collision mask* — a bitmask of layer indices this shape tests
      collision against. Collision between two shapes A and B fires when
      `(1 << B.layer) & A.mask` is non-zero, and optionally the symmetric
      check on B's mask (engine policy). Asymmetric filtering is intentional:
      a sensor can listen to the Player layer without Player needing to
      reciprocate.
    - *Sensor flag* — marks the shape as a trigger volume that reports
      overlaps without generating contact impulses.
    - *Material reference* — asset key of the physics material that governs
      surface response for this shape.
  - **Physics materials.** Each material carries:
    - *Static friction* — resistance to initiating slide (coefficient, 0–1+).
    - *Dynamic friction* — resistance while sliding (coefficient, 0–1+).
    - *Restitution* — coefficient of bounciness (0 = perfectly inelastic,
      1 = perfectly elastic).
    - *Density* — kg/m³, used to derive mass from shape volume when no
      explicit mass is authored.
    - *Friction combine mode* — how two colliding materials' friction values
      are blended: Average, Minimum, Maximum, or Multiply.
    - *Restitution combine mode* — same four options, applied independently
      to restitution.
  - **Rigid bodies.** Each record carries:
    - *Body type* — Static (immovable, zero cost), Dynamic (fully simulated),
      or Kinematic (script/animation-driven; pushes dynamics, is not pushed).
    - *Collision detection quality* — Discrete (default, one overlap test per
      step) or Linear Cast / Continuous (swept test, prevents tunnelling for
      fast-moving bodies).
    - *Mass* — explicit kg value; 0 means infer from shape volume × density.
    - *Center-of-mass override* — optional explicit CoM offset from the shape
      origin in local space; if absent the backend computes it from geometry.
    - *Inertia tensor override* — optional diagonal inertia (I_x, I_y, I_z)
      in kg·m²; if absent the backend derives it from shape and mass.
    - *Linear damping* — velocity decay coefficient per second (0 = no drag).
    - *Angular damping* — angular velocity decay coefficient per second.
    - *Gravity factor* — scalar multiplier on the scene gravity vector (1 =
      normal, 0 = weightless, negative = anti-gravity).
    - *Initial activation state* — Awake or Sleeping at spawn time.
    - *Max linear velocity* — clamp on speed to prevent numerical explosion.
    - *Max angular velocity* — clamp on spin rate.
    - *Allowed degrees of freedom* — per-axis locks on linear and angular
      motion (e.g., freeze Y translation, freeze Z rotation) without using a
      joint.
    - *Collision layer* and *collision mask* for body-level filtering.
    - *Shape reference* — asset key of the collision shape.
    - *Material reference* — per-body material override; takes precedence over
      the per-shape material when present.
    - *Sensor flag* — body-level override to make the entire body a trigger.
    - *Backend-specific scalar fields:*
      - Jolt: `mNumVelocityStepsOverride` — per-body velocity solver iteration
        override (0 = use world default); `mNumPositionStepsOverride` — same
        for position iterations.
      - PhysX: `solverIterationCounts.minVelocityIters`,
        `solverIterationCounts.minPositionIters` — per-actor solver iteration
        counts; `maxContactImpulse` — clamps contact impulse magnitude;
        `contactReportThreshold` — impulse threshold above which a contact
        event is reported to the simulation callback.
  - **Colliders** (static trigger/sensor volumes without a simulated body):
    - Shape reference, material reference, collision layer, collision mask,
      and sensor flag. No mass or motion properties.
  - **Character controllers.** Each record carries:
    - *Shape reference* — typically a capsule or tapered capsule.
    - *Mass* — used for push forces against dynamic bodies.
    - *Max slope angle* — steepest walkable surface incline (radians).
    - *Step height* — maximum upward step the character can traverse without
      requiring a jump.
    - *Step-down distance* — how far the character is snapped to ground before
      becoming airborne (for stairs and ramps).
    - *Max strength* — maximum contact impulse the character can exert on
      dynamic obstacles.
    - *Skin width* — separation margin from surfaces to avoid numerical
      contact jitter.
    - *Predictive contact distance* — lookahead distance for speculative
      contact generation.
    - *Collision layer* and *collision mask*.
    - *Inner shape reference* — optional tighter inner shape for soft contact
      response (Jolt inner-body pattern).
    - *Backend-specific scalar fields:*
      - Jolt: `mPenetrationRecoverySpeed` — fraction of penetration depth
        resolved per update (0–1); `mMaxNumHits` — maximum contact manifold
        points retained per update; `mHitReductionCosMaxAngle` — cosine of
        the angle below which near-parallel contact normals are merged.
      - PhysX: `contactOffset` — distance from the surface at which speculative
        contacts are generated (replaces Jolt's predictive contact distance at
        the backend level).
  - **Joints (constraints).** Each joint definition carries:
    - *Constraint type* — one of:
      - **Fixed (Weld)** — all 6 DOF locked; optionally breakable.
      - **Point / Ball-and-Socket** — 3 translational DOF locked, 3
        rotational DOF free; swing and twist limits optional.
      - **Hinge (Revolute)** — 5 DOF locked, 1 rotational DOF free around
        the hinge axis; angular limits and motor optional.
      - **Slider (Prismatic)** — 5 DOF locked, 1 translational DOF free
        along the slide axis; linear limits and motor optional.
      - **Distance** — maintains a min/max separation between anchor points;
        optionally spring-backed to behave as a soft rod or rope.
      - **Cone (Swing-Cone + Twist)** — limits swing to a cone half-angle
        and twist to an arc range around the primary axis; used for limbs.
      - **Six-DOF (Configurable)** — each of the 6 DOF is independently set
        to Locked, Limited (with range), or Free; motors and springs per
        axis; the superset from which all others can be derived.
    - *Body A* and *Body B* scene node references (B may be `kWorldAnchor`
      for a fixed-world attachment).
    - *Constraint space* — World (anchor frames in world space) or Local
      (anchor frames in each body's local space; preferred for stability).
    - *Local frame A* — position and orientation of the constraint anchor
      point on body A, in A's local space.
    - *Local frame B* — same for body B.
    - *Limits* — per constrained axis: lower bound, upper bound.
    - *Spring properties* — stiffness (N/m or N·m/rad) and damping ratio for
      spring-backed limits and position motors.
    - *Motor settings* — per-axis motor mode (Off, Velocity, Position),
      target velocity or target angle/position, max force or max torque,
      drive frequency and damping ratio for position mode.
    - *Break threshold* — linear force and angular torque magnitude above
      which the constraint is permanently disabled (one-shot breakable joint).
    - *Collision between connected bodies* — flag to enable or suppress
      collision detection between the two joined bodies.
    - *Priority* (for constraint ordering in the solver).
    - *Backend-specific scalar fields:*
      - Jolt: `mNumVelocityStepsOverride`, `mNumPositionStepsOverride` — per-
        constraint solver iteration overrides (0 = world default).
      - PhysX: `invMassScale0`, `invMassScale1`, `invInertiaScale0`, `invInertiaScale1` — scale applied to the inverse
        mass/inertia of each connected body for this constraint's solve pass.
  - **Vehicles.** Each vehicle definition carries:
    - *Chassis node reference* — the scene node that represents the dynamic
      rigid body forming the main body.
    - *Controller type* — Wheeled (independent suspension + differential) or
      Tracked (left/right track drives).
    - *Engine settings:*
      - Max torque (N·m).
      - Minimum and maximum RPM.
      - Torque curve — a sampled (RPM, normalized torque) table.
      - Inertia (kg·m²) of the rotating mass.
    - *Transmission settings:*
      - Forward gear ratios (one entry per gear).
      - Reverse gear ratios.
      - Final drive ratio (applied after all gears).
      - Clutch strength (N·m·s/rad).
      - Shift-up RPM and shift-down RPM thresholds for automatic mode.
      - Clutch engagement time (seconds).
      - Gear-shift mode — Auto or Manual.
    - *Differentials* (one or more):
      - Left wheel index and right wheel index.
      - Differential ratio.
      - Left-to-right torque split (0 = all left, 1 = all right, 0.5 = even).
      - Limited-slip ratio — max/min axle speed ratio before redistributing
        torque to the slower wheel.
      - Engine torque ratio applied to this differential.
    - *Anti-roll bars* (one or more):
      - Left wheel index and right wheel index.
      - Stiffness (N·m/rad).
    - *Wheels* — ordered array of wheel definitions; every other structure in
      this vehicle (differentials, anti-roll bars) references wheels by their
      zero-based index in this array. Each wheel entry carries:
      - *Scene node reference* — the scene node driven by the suspension and
        spin transforms written back after each simulation step.
      - *Axle index* — which axle this wheel belongs to (front = 0,
        rear = 1, additional axles numbered sequentially).
      - *Side* — Left or Right on the axle.
      - *Suspension attachment point* — position in chassis local space where
        the suspension spring connects to the chassis.
      - *Suspension direction* — unit vector in chassis local space (typically
        downward) along which the wheel travels.
      - *Suspension rest length* (m) — unloaded spring length.
      - *Suspension preload length* (m) — initial spring compression at rest.
      - *Suspension spring stiffness* (N/m).
      - *Suspension spring damping* (N·s/m).
      - *Maximum suspension force* (N).
      - *Wheel radius* (m) and *width* (m).
      - *Wheel rotational inertia* (kg·m²).
      - *Steering axis* — unit vector in chassis local space about which
        steering rotation is applied.
      - *Maximum steering angle* (radians).
      - *Max brake torque* (N·m) from service brake.
      - *Max hand-brake torque* (N·m).
      - *Longitudinal friction curve* — sampled (slip ratio, friction) table.
      - *Lateral friction curve* — sampled (slip angle, friction) table.
      - *Backend-specific:* Jolt `mWheelCastor` — castor angle in radians
        affecting self-aligning torque; no PhysX equivalent.

      > **At L2 this array is split into two destinations.** The cooker does
      > not emit it as a trailing array on the vehicle binding record. Instead:
      >
      > - **Topology fields** (scene node reference, axle index, side,
      >   `mWheelCastor`) → one fixed-size entry per wheel in the sidecar's
      >   shared **vehicle wheel table**. The vehicle binding record stores
      >   only `(wheel_slice_offset, wheel_slice_count)` to locate its
      >   contiguous slice in that table. The binding record stays fixed-size.
      >
      > - **Physics simulation settings** (suspension, friction curves, radius,
      >   steering, braking, and all per-differential/ per-gear settings) →
      >   into the **vehicle settings binary** (backend-cooked blob). The
      >   backend cook API serializes these as variable-length structures;
      >   Oxygen treats the result as opaque.

  - **Soft bodies.** Each definition carries:
    - *Source mesh reference* — asset key of the triangle mesh or tetrahedral
      mesh from which the simulation particle graph is derived.
    - *Edge (stretch) compliance* — XPBD compliance for edge-length
      constraints; lower = stiffer, resists stretching along edges.
    - *Shear compliance* — resistance to in-plane shear (cross-edge distance
      constraints between diagonally opposite vertices of quads).
    - *Bend compliance* — resistance to out-of-plane bending; enforced via
      dihedral angle constraints between adjacent triangle pairs.
    - *Volume compliance* — stiffness of tetrahedron volume-preservation
      constraints (XPBD) or Poisson ratio (FEM). Controls
      compressibility.
    - *Pressure coefficient* — internal gas pressure driving volume expansion;
      models inflated objects (balls, airbags).
    - *Tether mode* — topology rule for long-range attachment constraints:
      None (no tethers), Euclidean (straight-line rest length),
      or Geodesic (surface-distance rest length).
    - *Tether max-distance multiplier* — slack factor applied to tether rest
      lengths (1.0 = no slack, >1.0 = allows extra stretch before tether
      engages).
    - *Global damping* — velocity damping applied uniformly to all particles
      each substep.
    - *Friction* and *restitution* — surface response when the soft body
      collides with rigid geometry.
    - *Vertex radius* — per-particle collision margin (effectively the
      particle's collision sphere radius).
    - *Pinned vertices* — list of particle indices set to infinite inverse-
      mass; they act as fixed attachment points and are not moved by the
      solver. Used for hanging cloth or anchored ropes.
    - *Kinematic driven vertices* — list of particle indices whose position is
      set externally each step (e.g., driven by a bone transform), acting as
      soft anchors that can move.
    - *Solver iteration count* — number of constraint projection iterations
      per simulation substep; more = stiffer but costlier.
    - *Collision mesh reference* — optional separate, coarser collision mesh
      for broad-phase queries (FEM backend). If absent, the simulation mesh
      serves as both.
    - *Self-collision* — flag enabling particle-to-particle collision within
      the same soft body.
    - *Backend-specific scalar settings* — authored as named fields per target
      backend; stored at L2 as a discriminated union in the soft-body
      descriptor (not a blob). Only parameters with no engine-neutral
      equivalent are listed here:
      - **Jolt only:**
        - *Velocity iteration count* — dedicated velocity-phase solver
          iterations (separate from the position iteration count above; Jolt
          exposes both independently; other backends do not).
        - *LRA stiffness fraction* — long-range attachment stiffness as a
          fraction of edge stiffness, used when tether mode is Geodesic.
        - *Skinned constraint enable flag* — whether Jolt's skinned rigging
          constraints (bone-driven vertex targets) are active.
      - **PhysX FEM only:**
        - *Young's modulus* (Pa) — elastic stiffness of the FEM material;
          replaces the XPBD compliance model with a physically-based continuum
          constitutive law. Has no equivalent in XPBD engines.
        - *Poisson's ratio* (0–0.5) — lateral-to-axial strain ratio
          controlling volume preservation in the FEM material; complements
          Young's modulus. No equivalent in XPBD.
        - *Stabilization velocity threshold* (m/s) — below this speed the
          particle velocity is zeroed to prevent drift; PhysX-specific
          stabilization mechanism.
  - **Aggregates (groups / islands).** Each definition carries:
    - *Root node reference* — scene node whose subtree defines group
      membership.
    - *Max body count* — pre-allocation hint for the backend aggregate
      structure.
    - *Self-collision filter* — whether bodies within the aggregate are
      allowed to collide with each other.
    - *Authority mode* — Simulation (physics engine owns and writes transforms)
      or Command (external system drives transforms; physics reads them as
      kinematic targets).

- **L2 — Cooking:** Immutable, binary, runtime-ready data generated from L1.
  Immutability is a property of a specific cooked artifact: once produced, a
  cooked binary blob is sealed and does not change at runtime. L1 assets may
  freely come and go during authoring, but each cooking run produces a
  self-contained, version-stamped output. L2 operates in two distinct modes
  that share the same artifact definitions but differ in output layout:

  - **Loose Cooking (development mode)** — the cooker writes cooked artifacts
    directly to a *LooseCookedLayout* directory tree on the local filesystem.
    This layout is designed for fast iteration: re-cooking a single asset
    updates only the affected files without invalidating unrelated outputs.
    The layout consists of:
    - *Index file* (`*.oxlcidx`) — a fixed-header binary catalogue with an
      `IndexHeader` (magic, schema version, content version, flags), an
      `AssetEntry` table (one entry per cooked asset, carrying the asset key,
      descriptor-relative path, virtual path, asset type tag, and a SHA-256
      descriptor integrity hash), and a `FileRecord` table (one entry per
      resource/data file, carrying a `FileKind` tag and container-relative
      path). The index is the single authoritative manifest for the layout; the
      runtime and PAK builder both consume it directly.
    - *Asset descriptor files* — individual binary files, one per cooked
      asset, at the paths recorded in the index `AssetEntry` table. For
      physics these are the `CollisionShapeAssetDesc`, `PhysicsMaterialAssetDesc`,
      `PhysicsSceneAssetDesc`, etc. records written to disk as flat binary.
    - *Physics resource table file* (`FileKind::kPhysicsTable`) — a flat array
      of `PhysicsResourceDesc` entries — one per opaque backend blob —
      recording the content hash, size, and type tag for each.
    - *Physics resource data file* (`FileKind::kPhysicsData`) — the
      concatenated opaque backend blobs (cooked shape binaries, constraint
      binary streams, soft-body settings) referenced by the table. Offsets in
      each `PhysicsResourceDesc` are absolute from the start of this file.
    - Equivalent table/data file pairs exist for buffers, textures, scripts,
      and script bindings, all catalogued in the same index.

  - **PAK Builder (production mode)** — a separate offline tool that reads a
    fully cooked LooseCookedLayout (validated against its index) and
    repackages all of its assets and resource files into a single PAK bundle.
    The PAK is the only format the shipping runtime consumes; it is never
    written directly by the cooker. The PAK builder:
    - Resolves the index to enumerate all assets and file records.
    - Concatenates asset descriptor bytes into the PAK's typed asset regions.
    - Concatenates resource table and data bytes into the PAK's resource
      regions, updating all offsets to be PAK-relative.
    - Writes the PAK catalog and directory structures, producing a self-
      contained, memory-mappable bundle.
    - Does not re-cook; it is purely a packaging and relocation step.

  The **artifact taxonomy** covers what both Loose Cooking and PAK modes
  produce. All cooked data falls into exactly one of two categories:

  **Fixed-layout records** are packed, engine-owned binary structs with
  explicit reserved-byte padding for stable layout across engine versions.
  They are directly memory-mappable; the engine reads their individual fields.
  They carry engine-neutral data only; backend-specific scalars are stored as
  a discriminated union field within the record, never as a nested binary blob.

  **Trailing-array serialization policy.** Where a fixed-layout record
  contains a variable-length list of items (vertex index lists, child shape
  arrays, etc.), the list is not stored inline. Instead:

  - The fixed portion of the record contains a `uint32 count` and a
    `uint32 byte_offset` for each array. The byte offset is **self-relative**:
    it is measured from the start of the descriptor record itself.
  - All arrays for a given record are written contiguously in the binary file
    immediately after the fixed portion, in the order their `(count, offset)`
    pairs appear in the struct.
  - The complete unit — fixed header plus all trailing arrays — is treated as
    one atomic, self-contained allocation. It is read and written together; no
    partial reads are permitted.
  - This makes the entire record memory-mappable as a flat buffer without any
    pointer fixup.

  The following fixed-layout records carry trailing arrays:

  | Record | Trailing array | Element type |
  | --- | --- | --- |
  | Compound analytic shape descriptor | Child shape descriptors (one per sub-shape: shape type, inline params, local transform offset/rotation/scale) | Fixed-size child descriptor struct |
  | Soft-body binding record | Pinned vertex index list | `uint32` (particle index) |
  | Soft-body binding record | Kinematic-driven vertex index list | `uint32` (particle index) |

  Vehicle wheels do **not** use a trailing array on the vehicle binding
  record. All wheel scene-node records for all vehicles in a scene are stored
  in the shared **vehicle wheel table** — a separate flat table in the sidecar
  — and the vehicle binding record references its slice by
  `(wheel_slice_offset, wheel_slice_count)`. Every entry in the wheel table is
  fixed-size, so no trailing-array pattern is involved.

  Per-differential and per-gear arrays (gear ratios, torque curve, anti-roll
  bars, differential settings) are variable-length and backend-specific in
  structure; they are owned entirely by the vehicle settings binary (backend-
  cooked). Oxygen does not serialize them independently.

  All other fixed-layout records in the physics pipeline have no variable-
  length content and are fully self-contained within their fixed-size struct.

  **Fixed-layout record artifacts:**

  - **Analytic shape descriptor** — one record per analytic shape asset.
    Primitive shapes (Sphere, Capsule, Box, Cylinder, Cone, Plane, World
    Boundary) store all parameters inline in the fixed portion; no trailing
    array. Compound shapes additionally carry a trailing child descriptor
    array (see trailing-array policy above); the fixed header stores the child
    count and byte offset.
  - **Physics material descriptor** — one record per material asset (static
    friction, dynamic friction, restitution, density, friction and restitution
    combine modes). Padded to a power-of-two size.
  - **Sidecar binding tables** — one asset per scene, paired 1:1 with its
    scene asset. A typed component-table directory where each entry locates a
    flat array of fixed-size binding records. Engine-neutral fields per record
    type are documented in full under L1 Authoring above. Each record
    additionally carries a backend-specific scalar union for fields that have
    no engine-neutral equivalent:

    | Binding record | Backend-specific scalar union (Jolt / PhysX) |
    | --- | --- |
    | Rigid body | Jolt: `mNumVelocityStepsOverride`, `mNumPositionStepsOverride` / PhysX: `solverIterationCounts.minVelocityIters`, `solverIterationCounts.minPositionIters`, `maxContactImpulse`, `contactReportThreshold` |
    | Collider | — |
    | Character | Jolt: `mPenetrationRecoverySpeed`, `mMaxNumHits`, `mHitReductionCosMaxAngle` / PhysX: `contactOffset` |
    | Soft body | Jolt: `mNumVelocitySteps`, `mNumPositionSteps`, `mGravityFactor` / PhysX: `youngsModulus`, `poissons`, `dynamicFriction` |
    | Joint | Jolt: `mNumVelocityStepsOverride`, `mNumPositionStepsOverride` / PhysX: `invMassScale0`, `invMassScale1`, `invInertiaScale0`, `invInertiaScale1` |
    | Vehicle | — |
    | Vehicle wheel | Jolt: `mWheelCastor` (castor angle in radians, affecting self-aligning torque) / PhysX: — |
    | Aggregate | — |

  **Backend-cooked binary artifacts** (one blob per asset, tagged by format):

  - **Non-analytic shape binary** — cooked collision geometry for a single
    Convex Hull, Triangle Mesh, Height Field, or Compound shape, including the
    embedded BVH/acceleration structure. No standalone BVH artifact exists;
    it is integral to the binary.
    - Jolt: `Shape::SaveBinaryState()` → self-contained byte stream.
    - PhysX: `PxCooking::{cookConvexMesh | cookTriangleMesh | cookHeightField}()`
      → `PxDefaultMemoryOutputStream`.
  - **Constraint/joint binary** — runtime-instantiable settings for one joint
    (Fixed, Point, Hinge, Slider, Distance, Cone, or Six-DOF), including
    limits, spring properties, and motor settings.
    - Jolt: serialized `ConstraintSettings` subclass for the specific joint
      type.
    - PhysX: serialized via `PxSerialization` or RepX.
  - **Vehicle settings binary** — complete driveline for one vehicle: engine,
    transmission, differentials, anti-roll bars, and per-wheel parameters.
    Separated from the joint binary because vehicles have distinct binding
    records and a dedicated wheel topology sub-table.
    - Jolt: serialized `VehicleConstraintSettings` + `WheeledVehicleControllerSettings`
      or `TrackedVehicleControllerSettings`.
    - PhysX: serialized `PxVehicleWheelsSimData` + `PxVehicleDriveSimData`.
  - **Soft-body topology binary** — the simulation particle graph for one
    soft-body asset: vertex positions and inverse masses, edge/dihedral/volume/
    tether constraint graphs, and the material table. This is the OUTPUT of the
    backend topology-optimization pass, not the raw authoring parameters.
    - Jolt: `SoftBodySharedSettings` binary after `Optimize()`.
    - PhysX: FEM tetrahedral simulation mesh + collision tetrahedral mesh.

  **Side-table emission registry.** Every auxiliary ordered structure in the
  physics pipeline, with its exact physical emission location:

  | # | Side table | Physical location | Located by |
  | --- | --- | --- | --- |
  | 1 | **Sidecar component-table directory** | Immediately after the sidecar asset header, at a fixed known offset | Entry count and offset stored in the sidecar asset header |
  | 2 | **Rigid body binding records** | Flat array of fixed-size records at the byte offset given by directory entry 2 | Directory entry: type tag + byte offset from sidecar start + record count |
  | 3 | **Collider binding records** | Flat array of fixed-size records at the byte offset given by directory entry 3 | Directory entry |
  | 4 | **Character binding records** | Flat array of fixed-size records at the byte offset given by directory entry 4 | Directory entry |
  | 5 | **Soft-body binding records** | Sequence of variable-length record units at the byte offset given by directory entry 5; each unit = fixed header + trailing arrays (rows 6–7) back-to-back | Directory entry gives the start of the sequence; each unit's size = fixed header size + sum of trailing array byte sizes |
  | 6 | **Soft-body pinned vertex index list** | Trailing array immediately after each soft-body record's fixed header; part of the same record unit | Self-relative `(count, byte_offset)` in the record's fixed header |
  | 7 | **Soft-body kinematic vertex index list** | Trailing array immediately after the pinned vertex list (rows 6 + 7 are contiguous in memory); part of the same record unit | Self-relative `(count, byte_offset)` in the record's fixed header |
  | 8 | **Joint binding records** | Flat array of fixed-size records at the byte offset given by directory entry 8 | Directory entry |
  | 9 | **Vehicle binding records** | Flat array of fixed-size records at the byte offset given by directory entry 9 | Directory entry |
  | 10 | **Vehicle wheel table** | Flat array of fixed-size wheel records at the byte offset given by directory entry 10; all vehicles' wheels are concatenated here | Directory entry gives the table start; each vehicle binding record (row 9) provides `(wheel_slice_offset, wheel_slice_count)` to locate its slice |
  | 11 | **Aggregate binding records** | Flat array of fixed-size records at the byte offset given by directory entry 11 | Directory entry |
  | 12 | **Compound shape child descriptor array** | Trailing array immediately after the compound shape descriptor's fixed header in the shape asset file | Self-relative `(count, byte_offset)` in the descriptor's fixed header |
  | 13 | **Physics resource descriptor table** | Separate binary file — `FileKind::kPhysicsTable` — one per cooked container; not inside any asset | Loose index `FileRecord` table; PAK catalog |
  | 14 | **Physics resource data region** | Separate binary file — `FileKind::kPhysicsData` — concatenated backend blobs; not inside any asset | Loose index `FileRecord` table; PAK catalog; each blob located by absolute byte offset + size recorded in its `PhysicsResourceDesc` entry in table 13 |

- **L3 — Hydration:** How cooked resources are instantiated and bound to scene
  nodes in the simulation backend. This is a one-way lift from inert data to
  live simulation objects.
  - The hydrator validates the sidecar against the loaded scene (key match and
    node-count match) before processing any records. A mismatch is a hard
    failure.
  - The hydrator then resolves each component table in dependency order:
    - **Shapes first** — for each referenced shape asset, decode the
      descriptor: if analytic, construct directly from inline parameters; if
      non-analytic, hand the opaque blob to the backend restore API. Cache the
      resulting shape handle by asset key to allow instancing across multiple
      bodies referencing the same shape.
    - **Materials** — decode each material descriptor and create a backend
      material object (friction, restitution, combine modes). Cache by asset
      key.
    - **Rigid bodies** — for each record: look up cached shape and material
      handles, read world transform from the scene node index, construct the
      backend body with all binding-record properties (motion type, detection
      quality, mass, CoM override, inertia override, damping, gravity factor,
      DOF locks, activation state), add to the simulation world, store the
      (scene node index → body handle) mapping.
    - **Colliders** — same shape/material resolution; create a static sensor
      body; store (scene node index → shape handle) mapping.
    - **Characters** — restore shape, construct backend character controller
      with kinematic body + capsule sweep; store mapping.
    - **Soft bodies** — hand the backend settings blob to the backend restore
      API; create the soft body at the scene node's world transform; store
      mapping.
    - **Joints** — restore the backend constraint blob; look up body handle A
      and body handle B (or world anchor) from the node-index mapping; add
      the instantiated constraint to the simulation.
    - **Vehicles** — restore the vehicle constraint blob; look up the chassis
      body handle; instantiate the vehicle controller; iterate the wheel sub-
      table binding each wheel's scene node to the controller's wheel output
      for transform write-back.
    - **Aggregates** — create the backend aggregate or island group; collect
      all body handles in the subtree rooted at the aggregate node; register
      with the self-collision filter flag.
  - **Per-instance overrides** are read exclusively from the binding record,
    never from L1 source JSON. Shape geometry is shared across instances
    through the handle cache; per-body values (mass, damping, gravity factor,
    activation, sensor flag, collision layer and mask) are fully per-instance.
  - **Backend specificity** — the hydration code path branches on the format
    tag of each cooked blob. Jolt and PhysX have entirely separate restore
    code paths; there is no backend-agnostic deserializer for opaque blobs.
  - **Simulation-to-scene sync** — after each simulation substep, a sync pass
    reads every active body's world transform (position + orientation) from
    the backend and writes it back to the mapped scene node. Vehicle wheel
    transforms (spin angle + suspension offset) are also written to their
    respective scene nodes. Soft-body vertex positions may additionally drive
    a deformable mesh GPU buffer update. The rendering system consumes scene
    node transforms with no awareness of the physics backend.
  - **Positional index stability** — resource indices and scene node indices
    embedded in binding records are positional: they are stable if and only if
    cooking is deterministic and reproducible. Sidecar identity checks (scene
    content key + node count) are the runtime guard against index drift caused
    by partial re-cooks or version skew.

### Illustrative Scenario

> **Layout Assumptions:** All L2 cooked binary paths in these scenarios assume
> the default configuration of `oxygen::content::import::LooseCookedLayout`
> relative to the cooked root:
>
> - Physics shapes: `Physics/Shapes/*.ocshape`
> - Physics materials: `Physics/Materials/*.opmat`
> - Scenes (and sidecars): `Scenes/*.oscene` / `Scenes/*.opscene`
> - Physics resource files: `Physics/Resources/physics.table` and
>   `Physics/Resources/physics.data`

The following traces a single rigid-body sphere from authoring through live
simulation, exercising every abstraction defined above.

**Cast:**

| Actor | Role |
| --- | --- |
| Artist | Works in the engine editor; never touches binary data. |
| Cooker Tool | Offline CLI that reads an `import-manifest.json` and emits `LooseCookedLayout` artifacts. |
| Runtime Scene Loader | Loads the loose scene artifact and deserializes the scene graph. |
| Physics Sidecar Hydrator | Reads the loose physics sidecar artifact and drives L3 instantiation. |
| Physics Backend | Either **Jolt Physics** or **NVIDIA PhysX** — identical interface, divergent binary paths. |

---

**Step 1 — Authoring (L1):**

The artist opens the scene editor and drops a sphere onto the floor. She assigns:

- Shape: `Sphere`, radius `0.35 m`.
- Local transform: identity (origin at mesh pivot).
- Collision layer: `Dynamic`, mask: `All`.
- Physics material: `Rubber` (friction `0.8`, restitution `0.7`, combine mode
  `Maximum` for both).
- Body type: `Dynamic`, mass: `2.0 kg`, gravity factor: `1.0`, motion quality:
  `Continuous` (to prevent tunnelling at high velocity).

The editor serializes the result to three L1 source files:

- `bouncing_sphere.scene.json` — the scene graph (node hierarchy, world
  transforms, mesh references). This is the primary scene file.
- `bouncing_sphere.physics-sidecar.json` — the physics binding records,
  paired 1:1 with the scene file by matching basename. Contains the rigid
  body record for the sphere node.
- `rubber.physics-material.json` — the material definition.

No binary data exists yet. This is pure L1.

---

**Step 2 — Cooking (L2):**

The cooker tool is invoked with an `import-manifest.json` that defines the
cook jobs and their dependency graph for this scene. It processes the L1 files
according to the manifest and emits binary artifacts into the
`LooseCookedLayout`.

*Collision shape:*
Because `Sphere` is an analytic primitive, no mesh cooking is required. The
cooker writes a `CollisionShapeAssetDesc` with `ShapeType::kSphere` and a
`SphereParams { radius = 0.35 }` inline. This is stored in the loose layout
as `Physics/Shapes/sphere.ocshape`. No external physics resource blob is
allocated, so its `cooked_shape_ref` remains invalid (`kNoResourceIndex`).

*Physics material:*
The cooker writes a `PhysicsMaterialAssetDesc` with `friction = 0.8`,
`restitution = 0.7`, `combine_mode_friction = kMaximum`, and
`combine_mode_restitution = kMaximum`. This is 128 bytes, fixed layout, no
binary stream needed. It is emitted as `Physics/Materials/rubber.opmat`.

*Physics sidecar:*
The cooker computes the sidecar relationships and emits the compiled artifact
as `Scenes/bouncing_sphere.opscene`. At the head of this file is a
`PhysicsSceneAssetDesc` whose `target_scene_key` is the `AssetKey` of the
paired scene (xxHash3-128 of its virtual path — stable identity), whose
`target_scene_content_hash` is the SHA-256 of the scene binary built in the
same cook run (version guard), and whose `target_node_count` matches the exact
node count of that scene (quick early-reject). A single
`RigidBodyBindingRecord` is written into the `kRigidBody` component table in
the file, pointing to the sphere's scene node index, and referencing the
`CollisionShapeAssetDesc` and `PhysicsMaterialAssetDesc` `AssetKey` values.

*Backend divergence at L2:*
For non-analytic shapes (convex hulls, triangle meshes), the cooker must
produce a backend-specific binary blob: Jolt's cook path calls
`ConvexHullShapeSettings::Create()` followed by `Shape::SaveBinaryState()`;
PhysX's cook path calls `PxCooking::cookConvexMesh()` into a `PxOutputStream`.
Both blobs are stored as `PhysicsResourceDesc` entries in
`Physics/Resources/physics.table`, pointing to the raw payload in
`Physics/Resources/physics.data`, tagged by `PhysicsResourceFormat`
(`kJoltShapeBinary` vs. `kPhysXShapeBinary`). The sphere in this scenario
avoids this path entirely.

---

**Step 3 — Scene Loading (Runtime):**

The engine mounts the loose cooked workspace via its `container.index.bin`.
The Runtime Scene Loader requests the scene asset by its virtual path, and the
resource manager deserializes `SceneAssetDesc`, reconstructing the scene graph
in memory. The sphere node receives a world transform from the scene data. No
physics object exists yet; the node is just a transform in a hierarchy. The
loader publishes a `SceneLoaded` event.

---

**Step 4 — Sidecar Hydration (L3):**

The Physics Sidecar Hydrator responds to `SceneLoaded`. It opens the paired
`PhysicsSceneAssetDesc` and performs the identity check: if `target_scene_key`
or `target_node_count` mismatches the loaded scene, it hard-fails and logs an
error — the sidecar was cooked against a different scene version.

The hydrator then iterates the component tables:

1. **Resolve assets.** It requests the `CollisionShapeAssetDesc` (sphere,
   radius `0.35`) and `PhysicsMaterialAssetDesc` (rubber properties) from
   the resource manager, resolving them via the mounted loose container.

2. **Create the physics shape (backend call).**
   - *Jolt path:* `new SphereShape(0.35f)` — analytic, no binary restore needed.
   - *PhysX path:* `PxSphereGeometry sphere(0.35f)` → `PxPhysics::createShape(...)`.
   The `PhysicsMaterialAssetDesc` fields are mapped to the backend material
   descriptor at this point.

3. **Create the body.** The hydrator reads `RigidBodyBindingRecord`:
   - node index → world transform from the scene graph.
   - `body_type = kDynamic`, `motion_quality = kContinuous`, `mass = 2.0`,
     `gravity_factor = 1.0`, `linear_damping = 0.05`, `angular_damping = 0.05`.
   - *Jolt path:* `BodyCreationSettings` is populated; `BodyInterface::CreateAndAddBody()` is called; Jolt returns a `BodyID`.
   - *PhysX path:* `PxPhysics::createRigidDynamic()` is called with the
     extracted transform; mass properties are set via `PxRigidBodyExt::setMassAndUpdateInertia()`.

4. **Register.** The hydrator stores the mapping `(scene node index → body handle)` in an internal table so the simulation-to-scene-graph sync pass can write physics results back.

No L1 JSON is read at runtime. No L2 binary is re-interpreted beyond what the
backend API consumes. The per-instance override values (mass, damping, etc.)
come entirely from the `RigidBodyBindingRecord` — the cooked, compact, binary
binding record — not from the original JSON.

---

**Step 5 — Simulation:**

The physics engine owns the sphere body. It runs its step loop:
broad-phase (AABB), narrow-phase (sphere vs. floor plane), constraint solver
(contact/restitution impulse with combine mode `Maximum`), integration (symplectic
Euler in Jolt; semi-implicit Euler in PhysX). After each step the hydrator's
sync pass reads the updated body transform and writes it back to the sphere's
scene node — the rendering system sees the node move with no knowledge of the
physics backend.

---

**What Scenario I validates:**

| Concern | Where resolved |
| --- | --- |
| L1 ↔ L2 fidelity | Cooker reads every L1 field and emits a deterministic binary equivalent. |
| Backend portability | L2 format tag (`kJoltShapeBinary` / `kPhysXShapeBinary`) is the only divergence point; L3 hydration code branches on it. |
| Stale-sidecar safety | Identity check: `target_scene_key` (AssetKey — routing) + `target_scene_content_hash` (SHA-256 — version guard) + `target_node_count` (quick reject). |
| Cooking-order stability | `RigidBodyBindingRecord.node_index` is a stable scene node index; the cooker's deterministic ordering makes it reproducible. |
| Per-instance override | `RigidBodyBindingRecord` carries all instance-specific values; the shape asset is shared across instances via `AssetKey` lookup. |
| Analytic vs. mesh divergence | Analytic shapes (sphere, capsule, box) bypass the binary-blob cooking path entirely; `CollisionShapeAssetDesc` stores parameters inline. |

---

### Scenario II — Complex Scene: All Four Backend-Cooked Binary Paths

This scenario adds four actors to the same scene and exercises every L2
backend-cooked binary path and both sidecar trailing-array cases.

**Scene actors:**

| Actor | Physics setup | L2 path exercised |
| --- | --- | --- |
| Heavy crate | Dynamic body with 6-piece compound convex hull | Non-analytic shape blob ×6 + compound descriptor trailing child array |
| Hinged door | Dynamic door + static frame, Hinge joint with limits and break threshold | Constraint binary (hinge settings blob) |
| Cloth flag | Soft-body cloth pinned to a mast at 8 vertices | Soft-body topology binary + binding record trailing pinned-vertex array |
| Racing car | Wheeled vehicle, 4 wheels, 2-differential drivetrain | Vehicle settings binary + 4 vehicle wheel table entries |

---

**Step 1 — Authoring (L1):**

- **Crate**: shape type `Compound`; 6 children each of type `Convex Hull` with
  a source mesh reference and a local transform. Body type `Dynamic`, 80 kg.
- **Door + frame + hinge**: door = `Dynamic`; frame = `Static`. Joint type
  `Hinge`; local frame A at the door hinge pivot; local frame B at the frame
  socket. Angular limits `[-π/12, 5π/6]`. Break threshold 5000 N·m.
- **Flag**: source mesh = a 20×20 quad grid (400 vertices, ~760 edges); edge
  compliance `0.0001`; global damping `0.02`; self-collision off. Pinned
  vertices: `[0, 20, 40, 60, 80, 100, 120, 140]` (8 top-edge particles).
- **Car**: controller type `Wheeled`; engine max torque 400 N·m; 6-speed
  gearbox; 4 wheels across 2 axles; 2 differentials; 2 anti-roll bars.

---

**Step 2 — Cooking (L2) — complex paths:**

*Compound crate shape:*
The cooker iterates the 6 child convex hull meshes. For each hull:

- **Jolt** — `ConvexHullShapeSettings::Create()` → `Shape::SaveBinaryState()`
  → one `PhysicsResourceDesc` entry in `Physics/Resources/physics.table`, one
  blob in `Physics/Resources/physics.data`, tagged `kJoltShapeBinary`.
- **PhysX** — `PxCooking::cookConvexMesh()` → `PxDefaultMemoryOutputStream`
  → one blob, tagged `kPhysXShapeBinary`.

The compound `CollisionShapeAssetDesc` fixed header stores `child_count = 6`
and `child_array_byte_offset = sizeof(CompoundShapeAssetDesc)`. Immediately
after the header the cooker writes 6 fixed-size child descriptors — each
carrying the child blob `AssetKey` and local transform — as the **trailing
child array**. The complete descriptor is emitted to
`Physics/Shapes/crate.ocshape`.

*Hinge joint:*

- **Jolt** — `HingeConstraintSettings` (limits + break torque) serialized via
  `ConstraintSettings::SaveBinaryState()` → one blob in `Physics/Resources/physics.data`,
  tagged `kJoltConstraintBinary`.
- **PhysX** — `PxRevoluteJoint` descriptor serialized via RepX → one blob,
  tagged `kPhysXConstraintBinary`.

The joint binding record in the sidecar (`Scenes/complex_scene.opscene`)
stores the constraint binary `AssetKey`, the door body's scene node index, the
frame body's node index, and both local frame transforms.

*Cloth flag soft body:*

The cooker derives a particle graph from the 20×20 mesh (400 particles, edge
and dihedral-bend constraints).

- **Jolt** — `SoftBodySharedSettings::Optimize()` → blob tagged
  `kJoltSoftBodyBinary` written to `Physics/Resources/physics.data`.
- **PhysX** — FEM tetrahedral mesh processed and serialized → blob tagged
  `kPhysXSoftBodyBinary`.

The soft-body binding record's fixed header in the sidecar has
`pinned_vertex_count = 8` and
`pinned_vertex_byte_offset = sizeof(SoftBodyBindingRecord)`. The 8 indices
`[0, 20, 40, 60, 80, 100, 120, 140]` are written as a `uint32[]` trailing
array immediately after the header. `kinematic_vertex_count = 0`; no second
trailing array is emitted.

*Wheeled vehicle:*

The chassis convex hulls are cooked identically to the crate. The vehicle
settings binary bundles all variable-length arrays (torque curve, gear ratios,
2 differential structs, 2 anti-roll bar structs, 4 wheel physics structs) into
one blob in `Physics/Resources/physics.data`:

- **Jolt** — `VehicleConstraintSettings` + `WheeledVehicleControllerSettings`
  serialized → blob tagged `kJoltVehicleBinary`.
- **PhysX** — `PxVehicleWheelsSimData` + `PxVehicleDriveSimData` → blob
  tagged `kPhysXVehicleBinary`.

The sidecar's shared vehicle wheel table receives 4 new fixed-size entries
(scene node ref, axle index, side, `mWheelCastor`). The vehicle binding record
stores `wheel_slice_offset` (index of the first of these 4 entries) and
`wheel_slice_count = 4`.

---

**Step 4 — Sidecar Hydration (L3):**

- **Crate**: hydrator reads the compound descriptor, follows
  `child_array_byte_offset` to the 6 child descriptors, resolves each child
  blob `AssetKey` to a backend shape handle (loading and restoring the blob if
  not cached), assembles the compound shape, creates the dynamic body.
- **Door + hinge**: both bodies hydrated as dynamic/static. Constraint record
  processed next: constraint blob loaded by `AssetKey`, passed to
  `ConstraintSettings::sRestoreFromBinaryState()` (Jolt) or RepX (PhysX);
  instantiated constraint added to the simulation referencing the two body
  handles looked up by scene node index.
- **Flag**: soft-body blob loaded by `AssetKey`, passed to the backend restore
  API. Hydrator reads `pinned_vertex_count = 8`, follows
  `pinned_vertex_byte_offset` to the trailing array, passes the 8 indices to
  the backend to zero their inverse masses. Soft body added to the simulation.
- **Car**: vehicle blob loaded by `AssetKey`, passed to the backend vehicle
  restore API. Hydrator reads `(wheel_slice_offset, wheel_slice_count)` from
  the binding record, slices the wheel table, binds each wheel's scene node to
  the vehicle controller's wheel output for transform write-back.

---

**What Scenario II validates (additional concerns):**

| Concern | Where resolved |
| --- | --- |
| Non-analytic blob cooking | Convex hull mesh → backend cook API → `PhysicsResourceDesc` blob in `Physics/Resources/physics.data`; format tag routes hydration to the correct backend restore call. |
| Compound trailing child array | `child_count` + `child_array_byte_offset` in fixed header; 6 child descriptors written contiguously after; hydrator's self-relative offset read is the sole navigation mechanism. |
| Constraint binary cooking | Hinge settings → `SaveBinaryState()` / RepX; joint binding record references the blob by `AssetKey` — a cross-regeneration-unit reference, never a positional index. |
| Break threshold in blob | Stored inside the backend blob; the owning system detects breakage via backend event callback at runtime — not from the binding record. |
| Soft-body blob cooking | Full particle graph topology inside the backend blob; Oxygen never reads particle-level fields; only the blob bytes and the `AssetKey` are tracked. |
| Soft-body trailing array | Pinned vertex list at self-relative `byte_offset`; hydrator uses `(count, byte_offset)` to locate the `uint32[]` and apply inverse-mass zeroing. |
| Vehicle settings binary | All variable-length arrays (gears, differentials, anti-roll bars, per-wheel physics) bundled in one backend blob; Oxygen never parses their internal structure. |
| Vehicle wheel table slice | `(wheel_slice_offset, wheel_slice_count)` in the vehicle binding record locates 4 fixed-size topology entries in the shared sidecar wheel table; the vehicle binding record stays fixed-size with no trailing array. |
| Cross-unit AssetKey reference | Constraint and soft-body binding records reference blob assets from different regeneration units via `AssetKey`; no positional index crosses any unit boundary. |
