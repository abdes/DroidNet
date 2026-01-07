# Rendering Customization Architecture

## Document Purpose

This document defines how objects are configured for rendering in Oxygen,
establishing a clear hierarchy from intrinsic properties to optional overrides.

**Principle**: Favor intrinsic properties over overrides. An "override" should
only exist when breaking away from default/inherited behavior.

---

## Table of Contents

1. [Property Hierarchy (Russian Doll Model)](#property-hierarchy-russian-doll-model)
2. [Layer 1: Intrinsic Component Properties](#layer-1-intrinsic-component-properties)
3. [Layer 2: Node Flags (Scene Hierarchy Behavior)](#layer-2-node-flags-scene-hierarchy-behavior)
4. [Layer 3: Override Attachments](#layer-3-override-attachments)
5. [Layer 4: Material Instances](#layer-4-material-instances)
6. [Layer 5: GPU Instancing Parameters](#layer-5-gpu-instancing-parameters)
7. [Light Channels (Intrinsic Property)](#light-channels-intrinsic-property)
8. [Override Attachments Design](#override-attachments-design)
   - [Property Naming Convention](#property-naming-convention)
   - [C++20 Data Model](#c20-data-model)
   - [Scene Integration](#scene-integration)
9. [Rendering Domain Properties](#rendering-domain-properties)
10. [Streaming Domain Properties](#streaming-domain-properties)
11. [How Systems Consume Attachments](#how-systems-consume-attachments)
12. [Inheritance Semantics](#inheritance-semantics)
13. [Serialization](#serialization)
14. [Editor Integration](#editor-integration)
15. [Pipeline Flows](#pipeline-flows)
16. [Material Instances (Asset Design)](#material-instances-asset-design)
17. [GPU Instancing](#gpu-instancing)
18. [Implementation Status](#implementation-status)
19. [Summary: What Is vs What Isn't an Override](#summary-what-is-vs-what-isnt-an-override)
20. [References](#references)

---

## Property Hierarchy (Russian Doll Model)

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ Layer 1: INTRINSIC PROPERTIES                                               │
│                                                                             │
│ What the object fundamentally IS. Defined by component type.                │
│ Camera: FOV, planes. Light: color, intensity. Renderable: geometry, LOD.    │
│ Transform: position, rotation, scale. Light channels: which channels.       │
│                                                                             │
│ MECHANISM: Component member variables.                                      │
│ LIFETIME: Exists as long as component exists.                               │
│ EXTENSIBILITY: Add new components or new properties to existing components. │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Layer 2: NODE FLAGS (Inherited Behaviors)                                   │
│                                                                             │
│ Boolean behaviors that participate in scene hierarchy.                      │
│ Visible, static, casts/receives shadows, selectable.                        │
│                                                                             │
│ MECHANISM: SceneNodeFlags with 5-bit SceneFlag inheritance system.          │
│ LIFETIME: Exists on all nodes.                                              │
│ EXTENSIBILITY: Add new flag enum values.                                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Layer 3: OVERRIDE ATTACHMENTS (Domain-Tagged Property Bags)                 │
│                                                                             │
│ Optional, sparse property bags attached to nodes (stored at Scene level).   │
│ Each attachment has a domain tag; consuming system picks relevant props.    │
│                                                                             │
│ MECHANISM: OverrideAttachmentStore (sparse map keyed by node_id + domain).  │
│ LIFETIME: Optional per-node, stored in Scene.                               │
│ EXTENSIBILITY: Add new property keys; system code picks what it uses.       │
│                                                                             │
│ Domains: rndr_ (Rendering), strm_ (Streaming), phys_ (Physics),             │
│          aud_ (Audio), game_ (Gameplay), edtr_ (Editor)                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Layer 4: MATERIAL INSTANCES (Asset-Level Variation)                         │
│                                                                             │
│ Lightweight parameter variations of parent materials.                       │
│ Separate asset type, referenced via RenderableComponent material overrides. │
│                                                                             │
│ MECHANISM: MaterialInstanceAsset with parent reference + sparse overrides.  │
│ LIFETIME: Asset lifetime (loaded/unloaded with content).                    │
│ EXTENSIBILITY: Material system defines available parameters.                │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Layer 5: GPU INSTANCING PARAMETERS (Runtime Transient)                      │
│                                                                             │
│ Per-instance variation computed at runtime, lives only in GPU memory.       │
│ Fixed 64-byte budget per instance.                                          │
│                                                                             │
│ MECHANISM: ScenePrep packs into InstanceData buffer.                        │
│ LIFETIME: Single frame.                                                     │
│ EXTENSIBILITY: Shader defines parameter layout within budget.               │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Layer 1: Intrinsic Component Properties

These are **not overrides** — they define what the object fundamentally is.

### SceneNode (All Nodes)

Every node has a Transform, accessed via `node.GetTransform()`:

| Property | Type | Description |
| -------- | ---- | ----------- |
| Local position | `Vec3` | Translation relative to parent |
| Local rotation | `Quat` | Orientation relative to parent |
| Local scale | `Vec3` | Scale relative to parent |

### RenderableComponent

Accessed via `node.GetRenderable()`:

| Property | Type | Description |
| -------- | ---- | ----------- |
| Geometry | `GeometryAsset*` | The mesh to render |
| LOD Policy | `variant<Fixed, Distance, SSE>` | How LOD is selected |

**Note**: LOD policy is intrinsic to the renderable, not an "override". It
defines how THIS renderable behaves. The geometry asset defines available LODs;
the policy defines selection behavior.

### Light Components

Rather than enumerate all light types, here is **DirectionalLight** as a
concrete example of intrinsic properties.

**Common to all lights** (`CommonLightProperties`):

| Property | Type | Description |
| -------- | ---- | ----------- |
| `affects_world` | `bool` | Whether light contributes to scene |
| `color_rgb` | `Vec3` | Emitted color |
| `intensity` | `float` | Brightness multiplier |
| `mobility` | `LightMobility` | Realtime / Mixed / Baked |
| `casts_shadows` | `bool` | Whether light creates shadows |
| `shadow` | `ShadowSettings` | bias, normal_bias, contact_shadows, resolution_hint |
| `exposure_compensation_ev` | `float` | EV adjustment |

**DirectionalLight-specific**:

| Property | Type | Description |
| -------- | ---- | ----------- |
| `angular_size_radians` | `float` | Sun/moon angular size |
| `environment_contribution` | `bool` | Contributes to environment systems |
| `csm` | `CascadedShadowSettings` | cascade_count, cascade_distances, distribution_exponent |

Other light types (PointLight, SpotLight) follow the same pattern: common
properties plus type-specific ones. See the actual component headers for
complete definitions.

### Camera Components

Similar pattern: common camera properties plus type-specific ones.
See `Perspective.h` and `Orthographic.h` for complete definitions.

---

## Layer 2: Node Flags (Scene Hierarchy Behavior)

Node flags are **inherited behaviors** that flow through the scene hierarchy.
They use the 5-bit `SceneFlag` system for deferred updates and transition
detection.

### Existing SceneNodeFlags

| Flag | Default | Inherited | Description |
| ---- | ------- | --------- | ----------- |
| `kVisible` | true | No* | Node participates in rendering |
| `kStatic` | false | No | Transform won't change (optimization) |
| `kCastsShadows` | inherited | Yes | Node casts shadows |
| `kReceivesShadows` | inherited | Yes | Node receives shadows |
| `kRayCastingSelectable` | inherited | Yes | Selectable via ray cast |
| `kIgnoreParentTransform` | false | No | Use only local transform |

*Visibility uses effective value propagation (parent hidden → children hidden)
but each node can explicitly show/hide itself.

### Why These Are Flags, Not Overrides

Flags represent **inherent node behaviors** that participate in scene hierarchy
semantics. They're not "overriding" anything — they ARE the node's configuration.

A child node doesn't "override" its parent's shadow casting; it either inherits
that behavior or defines its own local value.

### Proposed Additional Flags

To complete the system, consider adding:

| Flag | Default | Inherited | Description |
| ---- | ------- | --------- | ----------- |
| `kVisibleInGame` | true | No | Hidden in game but visible in editor |
| `kVisibleInEditor` | true | No | Hidden in editor but visible in game |

These are visibility modes, not "rendering overrides".

---

## Layer 3: Override Attachments

Override attachments are **sparse property bags** stored at the Scene level,
keyed by (node_id, domain). They enable domain-specific configuration without
polluting node/component APIs.

**Key points:**

- Nodes can have zero or more attachments (one per domain)
- Each attachment is a property bag (key-value pairs)
- The consuming system interprets properties in its domain
- Stored in `OverrideAttachmentStore` on the Scene, not embedded in nodes
- Submesh/material overrides remain in `RenderableComponent` (intrinsic)

> See the comprehensive design in **[Override Attachments](#override-attachments-design)**
> for the C++20 data model, domain properties, and usage patterns.

---

## Layer 4: Material Instances

Material instances are **lightweight parameter variations** of a parent material.
They are a separate asset type, not a node property.

### The Problem They Solve

Without material instances, artists face a dilemma:

- **Clone the material** → Shader/PSO duplication, no batching, maintenance burden
- **Modify the original** → Affects all users of that material

Material instances solve this by sharing the parent's shader and PSO while
allowing parameter-level customization.

### Core Constraints

1. **Single-level inheritance only** — No MI → MI → MI chains (complexity trap)
2. **Schema-bound parameters** — Only parameters defined by parent are overridable
3. **Texture overrides break batching** — Clearly surfaced in editor
4. **Immutable at runtime** — Changes go through Layer 5 (GPU instancing)

### What's Overridable

| Category | Examples | Instanceable? |
| -------- | -------- | ------------- |
| Scalar params | roughness, metallic, emission | ✅ Yes |
| Vector params | tint color, UV scale/offset | ✅ Yes |
| Texture slots | albedo, normal map | ⚠️ Breaks batching |
| Shader/PSO | — | ❌ Never (use different material) |

See [Material Instances (Asset Design)](#material-instances-asset-design) for
the complete specification.

---

## Layer 5: GPU Instancing Parameters

This is **not authored** — it's computed at runtime by ScenePrep.

### What Goes Here

When multiple renderables can be instanced together, their material instance
parameter differences are packed into the per-instance GPU buffer:

| Slot | Contents | Source |
| ---- | -------- | ------ |
| `float4` | Color tint | MaterialInstance param delta |
| `float4` | Param multipliers | MaterialInstance param delta |
| `float4` | UV transform | MaterialInstance param delta |
| `float4` | Custom/game data | Runtime gameplay systems |

### Budget: 64 Bytes Per Instance

This is a **hard shader constraint**, not configurable per-node.

---

## Light Channels (Intrinsic Property)

Light channels determine which lights affect which objects. This is an
**intrinsic property** on both sides of the lighting equation.

```cpp
class RenderableComponent {
  uint8_t light_channel_mask_ = 0xFF;  // Which channels affect this object
};

class LightComponent {  // Base for all light types
  uint8_t light_channel_mask_ = 0xFF;  // Which channels this light contributes to
};
```

Light channels are intrinsic to how the object interacts with lighting — not an
override of anything. A renderable doesn't "override" its light channels; it
defines them as part of what it is.

---

## Override Attachments Design

### Philosophy

Override attachments are **sparse, optional property bags** attached to scene
nodes. They hold domain-tagged key-value pairs that consuming systems interpret.

**Design principles**:

1. **Property/value pairs** — no class-per-setting, just data objects
2. **Domain-tagged** — each attachment declares which system consumes it
3. **Sparse storage** — stored outside SceneNodeImpl (most nodes have none)
4. **Data objects** — no virtual functions, no inheritance hierarchies
5. **System interprets** — the domain implementation picks applicable properties

### Property Naming Convention

All property keys use a **domain prefix** for unambiguous debugging and logging:

| Domain | Prefix | Examples |
| ------ | ------ | -------- |
| Rendering | `rndr_` | `rndr_graph_id`, `rndr_shader_on`, `rndr_pass_id` |
| Streaming | `strm_` | `strm_priority`, `strm_dist_mult`, `strm_resident` |
| Physics | `phys_` | `phys_layer`, `phys_mass_mult`, `phys_kinematic` |
| Audio | `aud_` | `aud_reverb_zone`, `aud_occlusion`, `aud_priority` |
| Gameplay | `game_` | `game_faction`, `game_interact`, `game_trigger` |
| Editor | `edtr_` | `edtr_lock`, `edtr_gizmo`, `edtr_color` |

**Rules**:

- Prefix is 4-5 chars + underscore (fixed width for alignment in logs)
- Property name is concise but unambiguous (8-12 chars typical)
- Use snake_case for multi-word names: `rndr_shader_on`, not `rndr_shaderOn`

### Why Not Embedded in SceneNodeImpl?

Override attachments are:

- **Sparse**: Most nodes (90%+) have zero attachments
- **Variable-sized**: Property counts differ per attachment
- **Domain-specific**: Only one system cares about each attachment

Embedding `vector<unique_ptr<...>>` in SceneNodeImpl would:

- Waste memory on nodes without attachments (pointer overhead)
- Destroy cache locality that true components enjoy
- Suggest these are "full-fledged components" when they're not

**Solution**: Store attachments in a separate sparse container at the Scene
level, keyed by node ID.

### C++20 Data Model

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Property value types
// ─────────────────────────────────────────────────────────────────────────────

using PropertyValue = std::variant<
  bool,
  int32_t,
  uint32_t,
  float,
  std::string,
  glm::vec2,
  glm::vec4,
  AssetKey
>;

// ─────────────────────────────────────────────────────────────────────────────
// Override attachment: domain + property bag
// ─────────────────────────────────────────────────────────────────────────────

enum class OverrideDomain : uint8_t {
  kRendering,      // ScenePrep, passes, custom render graphs
  kStreaming,      // Asset streaming system
  kPhysics,        // Physics system
  kAudio,          // Audio system
  kGameplay,       // Game-specific systems
  kEditor,         // Editor-only, stripped in shipping builds
};

struct OverrideAttachment {
  OverrideDomain domain;
  bool inheritable = false;  // Propagates to children if not overridden

  // The property bag — domain system interprets what's relevant
  std::unordered_map<std::string_view, PropertyValue> properties;

  // Convenience accessors
  template<typename T>
  auto Get(std::string_view key) const -> std::optional<T> {
    if (auto it = properties.find(key); it != properties.end()) {
      if (auto* val = std::get_if<T>(&it->second)) {
        return *val;
      }
    }
    return std::nullopt;
  }

  template<typename T>
  auto GetOr(std::string_view key, T default_value) const -> T {
    return Get<T>(key).value_or(default_value);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Scene-level sparse storage
// ─────────────────────────────────────────────────────────────────────────────

class OverrideAttachmentStore {
public:
  // Attach to node (node may have multiple attachments in different domains)
  void Attach(NodeId node, OverrideAttachment attachment);

  // Query by node + domain
  auto Get(NodeId node, OverrideDomain domain) const
    -> const OverrideAttachment*;

  // Query with inheritance walk-up
  auto GetEffective(const SceneNode& node, OverrideDomain domain) const
    -> const OverrideAttachment*;

  // Iterate all attachments in a domain (for batch processing)
  auto AllInDomain(OverrideDomain domain) const
    -> std::span<std::pair<NodeId, const OverrideAttachment*>>;

private:
  // Sparse storage: most nodes have no attachments
  // Key: (node_id, domain) → attachment
  std::unordered_map<std::pair<NodeId, OverrideDomain>, OverrideAttachment> store_;
};
```

### Scene Integration

```cpp
class Scene {
  // ... existing members ...
  OverrideAttachmentStore override_attachments_;

public:
  auto GetOverrideAttachments() -> OverrideAttachmentStore& {
    return override_attachments_;
  }

  auto GetOverrideAttachments() const -> const OverrideAttachmentStore& {
    return override_attachments_;
  }
};
```

Nodes access attachments via the scene, not via member variables:

```cpp
// Query attachment for a node
auto* attachment = scene.GetOverrideAttachments().Get(node.Id(), OverrideDomain::kRendering);
if (attachment) {
  auto graph_id = attachment->Get<uint32_t>("render_graph_id");
  // ...
}
```

---

## Rendering Domain Properties

The rendering domain defines these well-known property keys:

### Submesh Overrides

For per-submesh material and visibility customization.

| Property Key | Type | Description |
| ------------ | ---- | ----------- |
| `rndr_submesh_vis` | `uint64_t` | Bitmask of visible submeshes |
| `rndr_mat_0` | `AssetKey` | Material for submesh 0 |
| `rndr_mat_1` | `AssetKey` | Material for submesh 1 |
| ... | ... | Up to submesh limit |

**Note**: The existing `RenderableComponent::submesh_state_` remains the
canonical location for submesh overrides (it's intrinsic to renderables).
The attachment model is for other rendering customizations.

### Render Graph Selection

For branching to custom render graphs in game code.

| Property Key | Type | Description |
| ------------ | ---- | ----------- |
| `rndr_graph_id` | `uint32_t` | ID of graph to use (game-defined) |
| `rndr_graph_name` | `std::string` | Alternative: name-based lookup |
| `rndr_subtree` | `bool` | Whether children inherit this |

**Usage pattern**: Render graphs in Oxygen are C++ coroutines, not editor-
authored assets. The game module defines available graphs and their IDs.
When ScenePrep encounters this property, it passes the ID to the game module
which branches to the appropriate coroutine.

```cpp
// In game module
void GameRenderer::SelectRenderGraph(const SceneNode& node, uint32_t graph_id) {
  switch (graph_id) {
    case kGraphId_Underwater:
      return RenderUnderwaterScene(node);
    case kGraphId_Mirror:
      return RenderMirrorReflection(node);
    default:
      return RenderStandard(node);
  }
}
```

### Shader Feature Overrides

Enable/disable shader features for specific nodes. **Must align with
ShaderCatalog** — the single source of truth for available features.

| Property Key | Type | Description |
| ------------ | ---- | ----------- |
| `rndr_shader_on` | `uint32_t` | Feature bits to enable |
| `rndr_shader_off` | `uint32_t` | Feature bits to disable |

**Constraint**: Only features defined in `ShaderCatalog` are valid. The
property values are validated against the catalog at load time.

```cpp
// ShaderCatalog defines available features
namespace ShaderFeature {
  constexpr uint32_t kSubsurfaceScattering = 1 << 0;
  constexpr uint32_t kMotionVectors        = 1 << 1;
  constexpr uint32_t kParallaxMapping      = 1 << 2;
  constexpr uint32_t kWetSurface           = 1 << 3;
  // ... defined by ShaderCatalog, not arbitrary
}

// In ScenePrep
void ApplyShaderFeatureOverrides(const OverrideAttachment* attachment,
                                 uint32_t& current_features) {
  if (attachment) {
    auto enable = attachment->GetOr<uint32_t>("rndr_shader_on", 0);
    auto disable = attachment->GetOr<uint32_t>("rndr_shader_off", 0);

    // Validate against ShaderCatalog
    assert((enable & ShaderCatalog::GetValidFeatureMask()) == enable);
    assert((disable & ShaderCatalog::GetValidFeatureMask()) == disable);

    current_features |= enable;
    current_features &= ~disable;
  }
}
```

### Scene-Level Cluster Configuration

Override the light culling cluster/tile configuration at scene level. Attach
to the **scene root node** with `inheritable = true` to affect the entire scene,
or to specific subtrees for localized configuration.

| Property Key | Type | Default | Description |
| ------------ | ---- | ------- | ----------- |
| `rndr_cluster_mode` | uint32_t | 0 | 0=tile-based, 1=clustered |
| `rndr_cluster_depth` | uint32_t | 24 | Number of depth slices (1-64) |
| `rndr_cluster_tile_px` | uint32_t | 16 | Tile size in pixels (8, 16, 32) |
| `rndr_cluster_max_lights` | uint32_t | 64 | Max lights per cluster |

**Tile-Based vs Clustered**:

- **Tile-based** (`rndr_cluster_mode = 0`): 2D grid with 16×16 tiles. Uses
  per-tile min/max depth from depth prepass. Simple, efficient for outdoor
  scenes with few overlapping lights.

- **Clustered** (`rndr_cluster_mode = 1`): 3D grid with logarithmic depth
  slices. Better for indoor scenes with many overlapping lights at different
  depths.

**Example**: Configure indoor area for clustered lighting

```cpp
OverrideAttachment cluster_config;
cluster_config.domain = OverrideDomain::kRendering;
cluster_config.inheritable = true;
cluster_config.properties["rndr_cluster_mode"] = uint32_t{1};      // Clustered
cluster_config.properties["rndr_cluster_depth"] = uint32_t{32};    // More slices
cluster_config.properties["rndr_cluster_tile_px"] = uint32_t{8};   // Smaller tiles
cluster_config.properties["rndr_cluster_max_lights"] = uint32_t{128};
scene.GetOverrideAttachments().Attach(indoor_root.Id(), std::move(cluster_config));

// Consuming: LightCullingPass reads from scene root
void Renderer::ConfigureLightCulling(const Scene& scene) {
  auto* att = scene.GetOverrideAttachments().Get(
    scene.GetRootNode().Id(), OverrideDomain::kRendering);

  ClusterConfig cfg = ClusterConfig::TileBased();  // Default

  if (att) {
    auto mode = att->GetOr<uint32_t>("rndr_cluster_mode", 0);
    if (mode == 1) {
      cfg = ClusterConfig::Clustered();
      cfg.depth_slices = att->GetOr<uint32_t>("rndr_cluster_depth", 24);
    }
    cfg.tile_size_px = att->GetOr<uint32_t>("rndr_cluster_tile_px", 16);
    cfg.max_lights_per_cluster = att->GetOr<uint32_t>("rndr_cluster_max_lights", 64);
  }

  light_culling_pass_->Configure(cfg);
}
```

**GPU Resources Produced**:

| Resource | Slot in EnvironmentDynamicData | Description |
| -------- | ------------------------------ | ----------- |
| Cluster Grid | `bindless_cluster_grid_slot` | `uint2(offset, count)` per cluster |
| Light Index List | `bindless_cluster_index_list_slot` | Packed light indices |

Shaders access these via `ClusterLookup.hlsli` utilities.

---

### Custom Pass Data

Arbitrary per-node data for custom render passes. This is the typical
use case for override attachments — game-specific effects that need
per-node configuration.

| Property Key | Type | Description |
| ------------ | ---- | ----------- |
| `rndr_pass_id` | `uint32_t` | Which pass consumes this data |
| `rndr_<pass>_*` | varies | Pass-defined properties (e.g., `rndr_dissolve_t`) |

**Example**: Dissolve effect

```cpp
// Authoring: attach dissolve parameters to node
OverrideAttachment dissolve;
dissolve.domain = OverrideDomain::kRendering;
dissolve.properties["rndr_pass_id"] = uint32_t{kPassId_Dissolve};
dissolve.properties["rndr_dissolve_t"] = 0.5f;
dissolve.properties["rndr_dissolve_edge"] = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
dissolve.properties["rndr_dissolve_width"] = 0.02f;
scene.GetOverrideAttachments().Attach(node.Id(), std::move(dissolve));

// Consuming: dissolve pass reads properties
void DissolvePass::Process(const SceneNode& node) {
  auto* att = scene.GetOverrideAttachments().Get(node.Id(), OverrideDomain::kRendering);
  if (att && att->GetOr<uint32_t>("rndr_pass_id", 0) == kPassId_Dissolve) {
    float progress = att->GetOr<float>("rndr_dissolve_t", 0.0f);
    glm::vec4 edge_color = att->GetOr<glm::vec4>("rndr_dissolve_edge", {1,1,1,1});
    // ... apply effect
  }
}
```

---

## Streaming Domain Properties

The streaming system interprets these properties:

| Property Key | Type | Description |
| ------------ | ---- | ----------- |
| `strm_priority` | `uint32_t` | 0=low, 1=normal, 2=high, 3=critical |
| `strm_dist_mult` | `float` | Multiplier for LOD/streaming distance |
| `strm_resident` | `bool` | Never unload this node's assets |

**Example**:

```cpp
OverrideAttachment streaming;
streaming.domain = OverrideDomain::kStreaming;
streaming.inheritable = true;  // Children inherit priority
streaming.properties["strm_priority"] = uint32_t{3};  // Critical
streaming.properties["strm_resident"] = true;
scene.GetOverrideAttachments().Attach(hero_node.Id(), std::move(streaming));
```

---

## How Systems Consume Attachments

Each system queries for attachments in its domain during processing:

```cpp
// In ScenePrep (rendering domain)
void ProcessNode(const Scene& scene, const SceneNode& node) {
  auto* att = scene.GetOverrideAttachments().GetEffective(
    node, OverrideDomain::kRendering);

  if (att) {
    // Render graph selection
    if (auto graph_id = att->Get<uint32_t>("rndr_graph_id")) {
      game_module_.SelectRenderGraph(node, *graph_id);
    }

    // Shader feature overrides
    ApplyShaderFeatureOverrides(att, current_features_);

    // Custom pass detection
    if (auto pass_id = att->Get<uint32_t>("rndr_pass_id")) {
      RegisterForCustomPass(*pass_id, node);
    }
  }

  // ... normal processing ...
}

// In StreamingSystem
void UpdatePriorities(const Scene& scene) {
  for (auto& [node_id, att] :
       scene.GetOverrideAttachments().AllInDomain(OverrideDomain::kStreaming)) {
    auto priority = att->GetOr<uint32_t>("strm_priority", 1);
    auto force = att->GetOr<bool>("strm_resident", false);
    SetNodeStreamingConfig(node_id, priority, force);
  }
}
```

---

## Inheritance Semantics

Attachments with `inheritable = true` propagate to children:

```cpp
auto OverrideAttachmentStore::GetEffective(
    const SceneNode& node,
    OverrideDomain domain) const -> const OverrideAttachment* {

  // Check this node first
  if (auto* att = Get(node.Id(), domain)) {
    return att;
  }

  // Walk up the tree looking for inheritable attachment
  for (auto parent = node.GetParent(); parent; parent = parent.GetParent()) {
    if (auto* att = Get(parent.Id(), domain)) {
      if (att->inheritable) {
        return att;
      }
      // Found non-inheritable attachment on ancestor — stop here
      return nullptr;
    }
  }

  return nullptr;
}
```

---

## Serialization

Override attachments serialize to a dedicated table in PAK format:

```text
OVRD chunk:
  uint32_t attachment_count;

  For each attachment:
    uint32_t node_index;
    uint8_t  domain;
    uint8_t  flags;         // bit 0: inheritable
    uint16_t property_count;

    For each property:
      uint16_t key_length;
      char     key[key_length];
      uint8_t  value_type;  // bool=0, i32=1, u32=2, f32=3, string=4, vec2=5, vec4=6, asset=7
      <value data>
```

The loader reads this table and populates `OverrideAttachmentStore`.

---

## Editor Integration

In the editor, override attachments appear as a property panel on selected
nodes. The editor:

1. Shows a dropdown to select domain
2. Shows domain-specific property templates (suggested keys)
3. Allows adding arbitrary key-value pairs
4. Validates known keys against domain expectations
5. For shader features, validates against `ShaderCatalog`

```text
┌─────────────────────────────────────────────────────────────────┐
│ Override Attachments                                    [+ Add] │
├─────────────────────────────────────────────────────────────────┤
│ ▼ Rendering                                                     │
│   ├─ rndr_graph_id: 2 (Underwater)                             │
│   ├─ rndr_shader_on: 0x0001 (SSS)                              │
│   └─ [+ Add Property]                                          │
│                                                                 │
│ ▼ Streaming [inheritable]                                       │
│   ├─ strm_priority: 3 (Critical)                               │
│   ├─ strm_resident: true                                       │
│   └─ [+ Add Property]                                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## Pipeline Flows

### Layer Responsibilities

| Layer | Defines | Consumed By | When |
| ----- | ------- | ----------- | ---- |
| 1. Intrinsic | What object IS | All systems | Always |
| 2. Flags | Inherited behaviors | Scene traversal, visibility | Always |
| 3. Blocks | Domain customization | Domain systems | When block present |
| 4. Material Instance | Material variation | MaterialBinder | When assigned |
| 5. GPU Instance | Per-draw variation | Shader | Per frame |

### What Each Layer Does NOT Define

| Layer | Does NOT Define |
| ----- | --------------- |
| 1. Intrinsic | Optional behaviors, domain-specific config |
| 2. Flags | Multi-valued properties, non-boolean config |
| 3. Blocks | Core object identity, permanent properties |
| 4. Material Instance | Non-material parameters, node-level config |
| 5. GPU Instance | Authored data, persistent configuration |

### Flow 1: Intrinsic Properties (No Override)

```text
EDITOR                          PAKGEN                          LOADER
─────────────────────────────────────────────────────────────────────────
1. Create SceneNode             1. Serialize node              1. Create SceneNode
2. Attach RenderableComponent   2. Pack component data         2. Create RenderableComponent
3. Set geometry asset           3. Store asset references      3. Resolve geometry asset
4. Set LOD policy               4. Store policy params         4. Set LOD policy
5. Set node flags               5. Store flag bits             5. Apply flags

No overrides involved — just direct property setting.
```

### Flow 2: Geometry Override (Material Replacement)

```text
EDITOR                          PAKGEN                          LOADER
─────────────────────────────────────────────────────────────────────────
1. Select node with renderable  1. Detect submesh overrides    1. Load geometry asset
2. View submesh list from geo   2. Serialize override table:   2. Load override table
3. Drag material to submesh     │  - node_index               3. For each override:
4. Material override stored     │  - lod                      │  - Find node
   in RenderableComponent       │  - submesh                  │  - Call SetMaterialOverride()
                                │  - material_key             4. Override material loaded
                                3. Add dependency on material    on-demand
```

### Flow 3: Material Instance Assignment

```text
EDITOR                          PAKGEN                          LOADER
─────────────────────────────────────────────────────────────────────────
1. Author MaterialInstanceAsset 1. Serialize MI asset:         1. Load parent material
   - Set parent material        │  - parent_key               2. Load MI asset
   - Override params (tint)     │  - param_overrides[]        3. Create MaterialInstance
   - Override textures (opt)    │  - texture_overrides[]         with parent reference
2. Assign MI to node submesh    2. Serialize node override:    4. Set material override
   (via material override)      │  - references MI asset         to MI
                                3. Add dependencies
```

### Flow 4: GPU Instancing Parameters

```text
SCENE PREP (Collection)         SCENE PREP (Finalization)      SHADER
─────────────────────────────────────────────────────────────────────────
1. Traverse visible nodes       1. Group by instance key:      1. Read instance_id
2. Resolve effective material:  │  hash(geo, mat_parent, lod) 2. Fetch InstanceData
   override → asset → default   2. For each group:            3. Apply tint:
3. Emit RenderItemData          │  - Allocate InstanceData       color *= inst.tint
4. If material is MI:           │  - Pack transform index     4. Apply param mults:
   - Note parent for grouping   │  - Pack param deltas:          rough *= inst.param.x
   - Compute param deltas       │    tint, param_mult, uv        metal *= inst.param.y
                                │  - Pack custom data          5. Apply UV transform:
                                3. Build DrawMetadata             uv = uv * scale + offset
                                4. Upload InstanceData buffer
```

---

## Material Instances (Asset Design)

### Design Philosophy

**Authoring perspective**: Artists need to create color/roughness/texture
variations without duplicating materials. The workflow should be:

1. Create parent material (defines shader, defaults, parameter schema)
2. Create instances that override specific parameters
3. Editor warns when texture overrides will break batching

**Runtime perspective**: The renderer needs to:

1. Batch instances with same parent + no texture overrides
2. Resolve parameters efficiently (parent defaults + sparse overrides)
3. Pack parameter deltas into per-instance GPU data

### Parameter Schema

The parent material defines which parameters exist and their types. Material
instances can only override parameters in this schema — no arbitrary additions.

```cpp
// Defined by the parent material's shader
enum class ParamType : uint8_t {
  kFloat,       // 4 bytes
  kFloat2,      // 8 bytes  (UV scale, etc.)
  kFloat3,      // 12 bytes (color RGB, etc.)
  kFloat4,      // 16 bytes (color RGBA, etc.)
};

struct ParamSchema {
  uint16_t id;           // Stable hash of parameter name
  ParamType type;
  uint8_t instance_slot; // Which float4 slot in InstanceData (0-3), or 0xFF if not instanceable
  float default_value[4];
};
```

**Key insight**: `instance_slot` maps parameters directly to the 64-byte
InstanceData layout. Only parameters with valid slots can vary per-instance
without breaking batching.

### Asset Structure

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// PAK format (on-disk)
// ─────────────────────────────────────────────────────────────────────────────

struct MaterialInstanceAssetDesc {
  AssetKey parent_key;            // Required: base material
  uint8_t  flags;                 // bit 0: has_texture_overrides
  uint8_t  reserved[3];
  uint16_t param_count;
  uint16_t texture_count;         // 0 for batchable instances
  // Followed by:
  //   ParamOverride[param_count]
  //   TextureOverride[texture_count]  (if any)
};

struct ParamOverride {
  uint16_t param_id;              // Must exist in parent's schema
  uint16_t reserved;
  float    value[4];              // Padded to 16 bytes for alignment
};

struct TextureOverride {
  uint8_t  slot;                  // Texture slot index
  uint8_t  reserved[7];
  AssetKey texture_key;
};
```

### Runtime Representation

```cpp
class MaterialInstance final {
public:
  // ───────────────────────────────────────────────────────────────────────────
  // Queries
  // ───────────────────────────────────────────────────────────────────────────

  auto GetParent() const -> const Material&;
  auto HasTextureOverrides() const -> bool;  // If true, breaks batching

  // Get resolved value (override if present, else parent default)
  auto GetParam(uint16_t param_id) const -> std::span<const float, 4>;

  // Get texture (override if present, else parent's)
  auto GetTexture(uint8_t slot) const -> TextureHandle;

  // ───────────────────────────────────────────────────────────────────────────
  // Instancing support
  // ───────────────────────────────────────────────────────────────────────────

  // Compute deltas from parent defaults for GPU instancing.
  // Returns values mapped to InstanceData layout (4 × float4).
  auto ComputeInstanceDeltas() const -> InstanceParams;

  // Instancing key: objects with same key can be batched.
  // Hash of (parent_key, texture_override_signature).
  auto GetBatchKey() const -> uint64_t;

private:
  std::shared_ptr<const Material> parent_;

  // Sparse: only overridden parameters stored
  // Key: param_id, Value: float[4]
  absl::flat_hash_map<uint16_t, std::array<float, 4>> param_overrides_;

  // Sparse: only overridden textures stored
  // Presence of ANY entry sets has_texture_overrides_ = true
  absl::flat_hash_map<uint8_t, TextureHandle> texture_overrides_;

  // Cached
  uint64_t batch_key_;
  bool has_texture_overrides_;
};
```

### Instance Delta Computation

When material instances are used with GPU instancing, their parameter
differences are packed into the 64-byte InstanceData buffer:

```cpp
auto MaterialInstance::ComputeInstanceDeltas() const -> InstanceParams {
  InstanceParams result = {};  // Zero-initialized (no delta)

  for (const auto& [param_id, value] : param_overrides_) {
    const auto& schema = parent_->GetParamSchema(param_id);

    if (schema.instance_slot == 0xFF) {
      continue;  // Not instanceable, already baked into material
    }

    // Compute delta or ratio depending on parameter semantics
    // Slot 0: tint (multiplicative), Slot 1: param_mult, etc.
    float* dst = &result.slots[schema.instance_slot].x;
    for (int i = 0; i < 4; ++i) {
      if (schema.type == ParamType::kFloat && i > 0) break;
      dst[i] = value[i] / schema.default_value[i];  // Ratio for multipliers
    }
  }

  return result;
}
```

### Batching Rules

| Scenario | Batchable? | Why |
| -------- | ---------- | --- |
| Same Material | ✅ Yes | Identical everything |
| MI + MI, same parent, params only | ✅ Yes | Deltas packed in InstanceData |
| MI + parent Material | ✅ Yes | MI with zero deltas |
| MI + MI, same parent, texture overrides | ❌ No | Different descriptor bindings |
| MI + MI, different parents | ❌ No | Different shader/PSO |

### Editor Integration

The material instance editor should:

1. **Show parent's parameter schema** — Only those parameters are editable
2. **Highlight instanceable params** — Visual indicator for params that won't break batching
3. **Warn on texture overrides** — "This will prevent GPU instancing with other instances"
4. **Show batch compatibility** — "Compatible with N other instances in scene"

```text
┌─────────────────────────────────────────────────────────────────┐
│ Material Instance: Wood_Dark                                    │
├─────────────────────────────────────────────────────────────────┤
│ Parent: M_Wood_Base                                             │
│ Batch Key: 0xA3F2...  (12 compatible in scene)                 │
├─────────────────────────────────────────────────────────────────┤
│ Parameters                                                      │
│   ├─ 🟢 Base Color      [0.2, 0.15, 0.1, 1.0]  (instanceable)  │
│   ├─ 🟢 Roughness       0.85                    (instanceable)  │
│   ├─ 🟢 Metallic        0.0                     (instanceable)  │
│   └─ 🟡 UV Scale        [2.0, 2.0]              (baked)         │
├─────────────────────────────────────────────────────────────────┤
│ Textures                                                        │
│   └─ (none overridden)                                          │
│                                                                 │
│ ⚠️ Texture overrides will break GPU instancing                  │
└─────────────────────────────────────────────────────────────────┘
```

### What Material Instances Do NOT Support

| Feature | Why Not | Alternative |
| ------- | ------- | ----------- |
| Runtime param changes | Immutable asset; use Layer 5 | Animate via InstanceData custom slot |
| Multi-level inheritance | Complexity trap; hard to debug | Flatten to single parent |
| Shader permutation changes | Breaks PSO sharing | Create separate material |
| Adding new parameters | Schema mismatch | Edit parent material |

### Future: Bindless Textures

With bindless textures, texture overrides could become instanceable:

```cpp
// Future InstanceData layout with bindless
struct InstanceData {
  uint32_t transform_index;
  uint32_t texture_indices[3];  // Bindless handles for albedo/normal/roughness
  float4 tint;
  float4 param_mult;
  float4 uv_transform;
  // custom slot removed to make room for texture indices
};
```

This would allow texture-varying instances to batch together. Design the
parameter schema now to support this future without breaking changes.

---

## GPU Instancing

### When Instancing Occurs

Instancing groups objects that share:

1. Same geometry asset
2. Same effective material parent
3. Same LOD level
4. Same shader permutation

### Per-Instance Data Budget

**64 bytes per instance** (4 × float4):

```cpp
struct InstanceData {
  uint32_t transform_index;     // 4 bytes
  uint32_t padding;             // 4 bytes (alignment)
  float4 tint;                  // 16 bytes: RGBA color multiplier
  float4 param_mult;            // 16 bytes: roughness, metallic, ao, emission
  float4 uv_transform;          // 16 bytes: scale.xy, offset.xy
  float4 custom;                // 16 bytes: game-specific
};
static_assert(sizeof(InstanceData) == 72);  // With padding
```

### What Breaks Instancing

| Change | Breaks Instancing? |
| ------ | ------------------ |
| Different transform | No |
| Different tint color | No |
| Different roughness/metallic mult | No |
| Different UV offset/scale | No |
| Different custom data | No |
| Different geometry | Yes |
| Different material parent | Yes |
| Different LOD | Yes |
| Material instance with texture override | Yes |

---

## Implementation Status

| Component | Status | Location |
| --------- | ------ | -------- |
| SceneNodeFlags | ✅ Complete | `SceneFlags.h` |
| Transform properties | ✅ Complete | `TransformComponent.h` |
| Renderable properties | ✅ Complete | `RenderableComponent.h` |
| Light properties | ✅ Complete | `LightCommon.h`, `*Light.h` |
| Camera properties | ✅ Complete | `Perspective.h`, `Orthographic.h` |
| Material override (per-submesh) | ✅ Complete | `RenderableComponent` |
| Submesh visibility | ✅ Complete | `RenderableComponent` |
| Light channel masks | ⏳ Pending | Design in this doc |
| OverrideAttachment struct | ⏳ Pending | Design in this doc |
| OverrideAttachmentStore | ⏳ Pending | Design in this doc |
| Rendering domain properties | 📋 Planned | Design in this doc |
| Streaming domain properties | 📋 Planned | Design in this doc |
| MaterialInstance asset | ⏳ Pending | Design in this doc |
| GPU instancing (basic) | ✅ Complete | Phase 3 |
| GPU instancing (64-byte params) | ⏳ Pending | Design in this doc |

---

## Summary: What Is vs What Isn't an Override

| Concept | Is It An Override? | Why |
| ------- | ------------------ | --- |
| Node visibility flag | No | Intrinsic node behavior |
| Node shadow flags | No | Intrinsic node behavior |
| LOD policy | No | Intrinsic renderable configuration |
| Submesh material | **Yes** | Overrides geometry asset default (via RenderableComponent) |
| Submesh visibility | **Yes** | Overrides geometry asset (via RenderableComponent) |
| Render graph selection | **Yes** | Override attachment property (`rndr_graph_id`) |
| Shader feature toggle | **Yes** | Override attachment property (`rndr_shader_on/off`) |
| Streaming priority | **Yes** | Override attachment property (`strm_priority`) |
| Custom pass data | **Yes** | Override attachment properties (`rndr_<pass>_*`) |
| Material instance params | No | Defines the material instance asset |
| Light color/intensity | No | Intrinsic light properties |
| Light channels | No | Intrinsic light/renderable property |
| Camera FOV | No | Intrinsic camera property |
| Per-instance tint | No | Runtime data, not authored override |

**Rule of thumb**:

- If it's something the object IS → **intrinsic property** (component members)
- If it's inherited on/off behavior → **node flag** (SceneNodeFlags)
- If it's per-submesh customization of geometry → **RenderableComponent**
- If it's optional domain-specific configuration → **override attachment** (property bag)
- If it's per-frame transient data → **GPU instancing parameters**

---

## References

- [SceneFlags.h](../../Scene/SceneFlags.h) - Flag system implementation
- [RenderableComponent.h](../../Scene/Detail/RenderableComponent.h) - Renderable properties
- [implementation_plan.md](implementation_plan.md) - Phased roadmap
- [scene_prep.md](scene_prep.md) - Collection and finalization pipeline
- [render_items.md](render_items.md) - RenderItem and DrawMetadata
