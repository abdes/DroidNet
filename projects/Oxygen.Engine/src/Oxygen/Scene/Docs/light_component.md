# Light Component

Defines light data attached to a SceneNode.

Types

- DirectionalLight
  - direction_ws: float3 (normalized)
  - color_rgb: float3 (linear)
  - intensity: float (unitless initially)
  - enabled: bool
- PointLight
  - position_ws: float3
  - radius: float (influence radius)
  - color_rgb: float3
  - intensity: float
  - enabled: bool
- SpotLight [deferred]

Defaults

- Directional: direction (0,-1,0), white color, intensity 1.0, enabled true
- Point: position (0,0,0), radius 5.0, white, intensity 1.0, enabled true

Integration

- Attached via `SceneNode::AddComponent(LightComponent)`.
- Extraction collects enabled lights per frame into Renderer.
- Do not mutate during extraction; treat data as read-only snapshots.

Notes

- Units are provisional; we’ll refine when tone mapping is in place.
- Layers/masks for filtering are deferred.
