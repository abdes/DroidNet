# ED-M08 delivery scope

### ED-M08 — V0.1 canonical authoring and rendering

**Status:** `planned`; design package ready for implementation.

The [editor ED-M08 plan](../../../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md)
owns the cross-engine/editor sequence and acceptance gates. Implement native
formats/producers and renderer behavior, refresh affected content, and pass native
visual validation outside the editor before editor integration. Native work is
defined by the [rendering contract](../../lld/editor-rendering.md),
[captured-sky IBL contract](../../lld/captured-sky-ibl.md) and
[deferred capability record](deferred-capabilities.md).

Scope includes independent directional lights and shadows, explicit atmospheric
slots, visibility/contribution/receiver behavior, contact shadows, captured-sky
diffuse/specular lighting, Stage 13 activation and Stage 12 ambient-bridge
retirement, exact cameras, grading, canonical material slots/emission and
procedural defaults. Qualification tools are opt-in development targets; normal
engine/editor Debug and Release builds contain no qualification payload.

The latest closed Vortex plan remains
[VTX-M08 static SkyLight](../VTX-M08/README.md). Its evidence
covers static specified-cubemap diffuse lighting. It does not close the ED-M08
extension. Preserve ordinary native demo loading, offscreen/composition behavior,
feature variants and material-sidedness/mirrored-winding correctness.

Broader GI/SSR/reflection probes, continuous time-sliced sky capture, cubemap blend
transitions, SkyLight occlusion/baking, VSM, geometry virtualization, material
composition, clouds, heterogeneous volumes, water, hair, distortion and light
shafts remain separately scoped future work. The ED-M08 IBL subset does not
activate those families.
