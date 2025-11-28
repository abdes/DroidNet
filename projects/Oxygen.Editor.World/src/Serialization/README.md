# Serialization (DTO-first) — Policies and conventions

This folder contains the canonical data transfer objects (DTOs) and source-generated
serialization context used for persisting scenes and related objects in the editor.

Follow these rules when adding or modifying serialization behaviour:

- DTOs in this folder are the single source-of-truth for on-disk persistence of scenes.
  - Use the DTO classes (e.g. `SceneData`, `SceneNodeData`, `GeometryComponentData`,
    `OverrideSlotData`) for file formats, migrations and compatibility.

- Use the Hydrate / Dehydrate pattern for converting between DTOs and runtime domain objects:
  - Domain objects implement `IPersistent<TDto>` where `Hydrate(TDto)` populates runtime state
    and `Dehydrate()` returns a DTO representation to persist.
  - Creation should follow the existing factory/registration pattern (see `GameComponent` and
    `OverrideSlot.CreateAndHydrate`) so concrete types register how to create themselves from DTOs.

- Important design decisions
  - DTOs contain only persisted state and intentionally omit runtime-only defaults or caches.
  - `OverridablePropertyData<T>` stores only the override intent; defaults live in the domain types
    and are not serialized.
  - Hydration must be performed with notifications suppressed (so property-change observers don't
    get noisy events while building objects).

- Avoid serializing domain objects directly for persistent scene files.
  - Domain-level JSON attributes may remain useful for ephemeral uses (testing, clipboard, debug),
    but they are not authoritative for on-disk scene persistence. Use the DTOs + Hydrate/Dehydrate
    when reading and writing scenes.

- Tests & CI
  - Add round-trip tests that verify DTO -> Hydrate -> Dehydrate -> DTO produces equivalent DTOs
    (or follows documented equivalence rules for omitted defaults).
  - Prefer explicit unit tests for conversion code (e.g. `OverridableProperty` helpers).

If you're unsure which approach to use for a particular feature, prefer adding a small DTO and
explicit Hydrate/Dehydrate implementation rather than attempting to persist domain types directly.
