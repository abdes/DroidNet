# RenderScene

This example mounts a cooked `.pak` file, resolves a `SceneAsset` key (via the
PAK embedded browse index virtual paths or manual key entry), loads the cooked
scene through the engine `AssetLoader`, instantiates a runtime `scene::Scene`
hierarchy, and renders it using the standard single-view example pipeline.

The ImGui overlay is intentionally focused on PAK mounting and scene loading.

## GLB to PakGen YAML

The helper script [Examples/RenderScene/glb_to_pak_spec.py](Examples/RenderScene/glb_to_pak_spec.py)
can generate a PakGen YAML spec from a `.glb`.

To avoid extremely large YAML files (hex-encoding doubles size and is slow to
parse), the default mode writes binary payloads (vertex/index buffers and
textures) to a sibling folder and references them via `file:` entries.

Example:

`python Examples/RenderScene/glb_to_pak_spec.py Examples/RenderScene/WaterBottle.glb bin/Oxygen/WaterBottle.yaml`

This produces:

- `bin/Oxygen/WaterBottle.yaml`
- `bin/Oxygen/WaterBottle_payload/*` (e.g. `.bin` for buffers, `.rgba8` for textures)

If you explicitly want to embed everything into YAML (slow, for debugging
only), use:

`python Examples/RenderScene/glb_to_pak_spec.py --data-mode hex <in.glb> <out.yaml>`

## One-command GLB -> PAK

If you want a single command that:

- generates the YAML spec + payload files, and
- builds the `.pak`

use [Examples/RenderScene/make_pak.py](Examples/RenderScene/make_pak.py).

Run it from the `Examples/RenderScene` directory:

`python .\make_pak.py .\glb\Tree1.glb`

You can also pass an existing PakGen YAML spec:

`python .\make_pak.py .\pak\Tree1.yaml`

Outputs:

- `pak/Tree1.yaml`
- `pak/Tree1_payload/*`
- `pak/Tree1.pak`
- `pak/Tree1.manifest.json`
