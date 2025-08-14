# Shading Model (Initial)

Scope: minimal, pragmatic lighting for early integration; performance and
physical accuracy come later.

Model

- Diffuse: Lambert, `diffuse = albedo * max(dot(N, L), 0)`
- Specular (optional): Blinn-Phong with `specPower` from material or a default
- Ambient: constant ambient term from SceneConstants

Inputs

- Normal: from vertex normal (if present) or a face-normal fallback
- Light(s):
  - Phase 5B: one directional light via SceneConstants
  - Phase 5C: multiple lights via bindless `g_Lights` buffer (loop with cap)

Spaces & Conventions

- All colors are linear; tone mapping and gamma are handled in post
- Directions and positions are in world space; transform to shading space as
  needed

Caps

- MAX_LIGHTS_PER_PIXEL: cap the loop (e.g., 64) and document

Future Work

- Proper material parameters (roughness/metalness)
- Normal maps and TBN generation
- Energy conserving BRDFs and IBL
