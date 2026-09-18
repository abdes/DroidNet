"""Audit both queued views' real HDR resources and shared narrowing allowance."""

from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    expected_mask = 1584  # Sky view, camera AP, fog, and accumulated SceneColor.
    products_by_report = {}
    finalized = set()
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        outputs = [x for x in writes if names.get(str(x.resource)) == "Vortex.Exposure.Suitability"]
        constants = [x for x in reads if x.byteSize == 128 and x.elementByteSize == 128]
        if outputs and constants:
            if len(outputs) != 1 or len(constants) != 1:
                raise RuntimeError("Ambiguous reference evaluation bindings")
            c = constants[0]
            raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 128))
            words = struct.unpack("<32I", raw)
            if words[20] != 0x3f800000 or words[21] == 0xffffffff or words[22:] != (0, 0):
                raise RuntimeError("Reference fixture gain/reserved ABI mismatch")
            if words[9] != expected_mask:
                raise RuntimeError(f"Missing required product in evaluation mask: {words[9]}")
            product_id = words[8]
            if product_id:
                share = struct.unpack_from("<f", raw, 68)[0]
                if product_id not in (5, 6, 10, 11) or share != .25:
                    raise RuntimeError(f"Incorrect product/budget partition: {product_id}, {share}")
                sources = [x for x in reads if str(x.resource) in textures]
                if len(sources) != 1:
                    raise RuntimeError("Expected one reference texture for this unmasked fixture")
                texture = textures[str(sources[0].resource)]
                if texture.format.compByteWidth != 4:
                    raise RuntimeError("Scene reference must precede FP16 narrowing")
                shape = (texture.width, texture.height, texture.depth)
                if tuple(words[4:7]) != shape:
                    raise RuntimeError("Evaluation dimensions do not match the bound texture")
                products = products_by_report.setdefault(str(outputs[0].resource), {})
                product = (str(sources[0].resource), shape, share)
                if product_id in products and products[product_id] != product:
                    raise RuntimeError("A view's product changed between maximum and error passes")
                products[product_id] = product

        inputs = [x for x in reads if names.get(str(x.resource)) == "Vortex.Exposure.Suitability"]
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        states = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if inputs and statuses:
            if len(inputs) != 1 or len(statuses) != 1 or len(states) != 1:
                raise RuntimeError("Ambiguous finalizer resources")
            key = str(inputs[0].resource)
            products = products_by_report.get(key, {})
            if set(products) != {5, 6, 10, 11} or sum(x[2] for x in products.values()) != 1:
                raise RuntimeError("Incomplete collected products or image-budget partition")
            values = struct.unpack("<2f10I", bytes(controller.GetBufferData(inputs[0].resource, 0, 48)))
            texels = sum(x[1][0] * x[1][1] * x[1][2] for x in products.values())
            if values[2] != expected_mask or values[10] != expected_mask or values[9] != texels:
                raise RuntimeError(f"Reference report did not check every required texel: {values}")
            gains = struct.unpack("<4f", bytes(controller.GetBufferData(states[0].resource, 0, 16)))
            if gains != (.0625,) * 4:
                raise RuntimeError(f"Qualification changed fixed exposure: {gains}")
            finalized.add(key)
            report.append(f"event={action.event_id} report={key} products={products} texels={texels} failures={values[3]}")
    if len(finalized) != 2:
        raise RuntimeError(f"Expected two independent scene reference reports, got {len(finalized)}")
    report.append("scene_hdr_products_verdict=pass")
    report.append("scope=real FP32 reference products; producer pre-store checks and format switching remain open")


if __name__ == "__main__":
    run_ui_script("_scene_hdr_products.txt", build_report)
