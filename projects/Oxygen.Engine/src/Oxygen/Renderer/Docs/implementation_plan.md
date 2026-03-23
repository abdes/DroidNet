# Renderer Implementation Plan

Status: active renderer shadow support is conventional directional cascaded shadow maps only.

## Shadow System

Completed:

- `LightManager` publishes directional shadow candidates.
- `ShadowManager` coordinates directional shadow work.
- `ConventionalShadowBackend` owns directional shadow planning and publication.
- `ConventionalShadowRasterPass` renders directional cascade depth.
- Forward shading consumes `ShadowInstanceMetadata` plus conventional directional metadata.
- Renderer shadow bindings and shader catalogs were reduced to the conventional path.
- Obsolete shadow code, shaders, tests, examples, configs, and debug surfaces were removed from the repository.

Follow-up guidance:

- treat the conventional directional path as the only supported shadow backend
- keep docs and examples aligned with that scope
- if a future shadow family is introduced, document it as a new design slice instead of reviving removed assumptions
