"""Source-analysis command integration and input-protection regressions."""

import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import struct
import tempfile
import unittest
import zlib


IMPORT_TOOL = Path(sys.argv.pop(1)).resolve()


class SourceAnalysisCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="oxygen-source-analysis-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def manifest(self, jobs):
        path = self.root / "manifest.json"
        path.write_text(json.dumps({"version": 1, "jobs": jobs}), encoding="utf-8")
        return path

    def analyze(self, manifest, report):
        return subprocess.run(
            [str(IMPORT_TOOL), "analyze-sources", "--manifest", str(manifest),
             "--report", str(report)],
            cwd=self.root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=20, check=False,
        )

    def test_mixed_batch_needs_no_cooked_destination(self):
        (self.root / "material.json").write_text('{"name":"Stone"}', encoding="utf-8")
        (self.root / "scene.json").write_text(
            '{"version":9,"name":"Empty","nodes":[]}', encoding="utf-8")
        manifest = self.manifest([
            {"type": "material-descriptor", "source": "material.json"},
            {"type": "scene-descriptor", "source": "scene.json"},
        ])
        report = self.root / "analysis.json"
        result = self.analyze(manifest, report)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(data["complete"])
        self.assertEqual(len(data["jobs"]), 2)
        self.assertFalse((self.root / ".cooked").exists())

    def test_unsupported_job_cannot_overwrite_its_primary_source(self):
        source = self.root / "script.luau"
        source.write_text("return 42", encoding="utf-8")
        manifest = self.manifest([{"type": "script", "source": source.name}])
        report = self.root / "failure.json"
        result = self.analyze(manifest, report)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(report.exists(), result.stdout + result.stderr)
        self.assertFalse(json.loads(report.read_text(encoding="utf-8"))["complete"])
        result = self.analyze(manifest, source)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(source.read_text(encoding="utf-8"), "return 42")

    @unittest.skipUnless(os.name == "nt", "Windows case-insensitive path identity")
    def test_report_cannot_create_a_missing_source_through_case_alias(self):
        manifest = self.manifest([{"type": "texture", "source": "Missing.PNG"}])
        report = self.root / "missing.png"
        result = self.analyze(manifest, report)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report.exists())

    def test_report_cannot_create_a_negatively_probed_cubemap_candidate(self):
        for suffix in ("posx", "negx", "posy", "negy", "posz", "negz"):
            (self.root / f"sky_{suffix}.png").write_bytes(b"not decoded")
        manifest = self.manifest([{"type": "texture", "source": "sky.png", "cubemap": True}])
        ordinary_report = self.root / "cube-analysis.json"
        result = self.analyze(manifest, ordinary_report)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.analyze(manifest, self.root / "sky_px.png")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "sky_px.png").exists())


class CapturedInputBatchCliTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="oxygen-captured-batch-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.inputs = []

    def capture(self, name, content):
        if isinstance(content, str):
            content = content.encode("utf-8")
        path = self.root / f"capture-{len(self.inputs)}.bin"
        path.write_bytes(content)
        entry = {
            "logical_path": str(self.root / name), "exists": True,
            "metadata": {"size": len(content), "is_directory": False,
                         "is_symlink": False, "last_modified_seconds": 0,
                         "last_modified_nanoseconds": 0},
            "file": {"path": str(path), "size": len(content),
                     "sha256": hashlib.sha256(content).hexdigest()},
        }
        self.inputs.append(entry)
        return entry

    def batch(self, jobs):
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps({
            "version": 1, "output": str(self.root / "cooked"), "jobs": jobs,
        }), encoding="utf-8")
        captures = self.root / "captures.json"
        captures.write_text(json.dumps({"schema_version": 1, "inputs": self.inputs}), encoding="utf-8")
        return subprocess.run(
            [str(IMPORT_TOOL), "--no-tui", "batch", "--manifest", str(manifest),
             "--captured-inputs", str(captures)],
            cwd=self.root, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=30, check=False,
        )

    def analyze(self, jobs, report=None):
        manifest = self.root / "analysis-manifest.json"
        manifest.write_text(json.dumps({"version": 1, "jobs": jobs}), encoding="utf-8")
        captures = self.root / "captures.json"
        captures.write_text(json.dumps({"schema_version": 1, "inputs": self.inputs}), encoding="utf-8")
        report = report or self.root / "analysis.json"
        return subprocess.run(
            [str(IMPORT_TOOL), "analyze-sources", "--manifest", str(manifest),
             "--root", str(self.root), "--captured-inputs", str(captures),
             "--report", str(report)],
            cwd=self.root, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=20, check=False,
        )

    def test_analysis_uses_replacement_bytes_without_changing_live_source(self):
        source = self.root / "material.json"
        original = '{"name":"Published"}'
        source.write_text(original, encoding="utf-8")
        entry = self.capture(source.name, '{"name":"Replacement"}')
        jobs = [{"type": "material-descriptor", "source": source.name}]
        result = self.analyze(jobs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        first = json.loads((self.root / "analysis.json").read_text(encoding="utf-8"))
        job = first["jobs"][0]
        self.assertTrue(first["complete"])
        self.assertEqual(Path(job["source_path"]), source)
        self.assertTrue(any(output["virtual_path"].endswith("/Replacement.omat") for output in job["outputs"]))
        self.assertEqual(source.read_text(encoding="utf-8"), original)

        relocated = self.root / "another-operation" / "bytes.bin"
        relocated.parent.mkdir()
        relocated.write_bytes(Path(entry["file"]["path"]).read_bytes())
        entry["file"]["path"] = str(relocated)
        result = self.analyze(jobs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        second = json.loads((self.root / "analysis.json").read_text(encoding="utf-8"))
        self.assertEqual(first, second)

    def test_analysis_protects_all_capture_inputs_from_report_overwrite(self):
        used = self.capture("material.json", '{"name":"Replacement"}')
        unused = self.capture("other.json", '{"name":"Unconsumed"}')
        jobs = [{"type": "material-descriptor", "source": "material.json"}]
        for entry in (used, unused):
            target = Path(entry["file"]["path"])
            before = target.read_bytes()
            with self.subTest(target=target):
                result = self.analyze(jobs, target)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(target.read_bytes(), before)
            with self.subTest(logical=entry["logical_path"]):
                result = self.analyze(jobs, Path(entry["logical_path"]))
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(Path(entry["logical_path"]).exists())
        captures = self.root / "captures.json"
        result = self.analyze(jobs, captures)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(captures.read_text(encoding="utf-8"))["inputs"], self.inputs)

    def test_analysis_rejects_uncaptured_live_inputs_and_changed_captures(self):
        source = self.root / "material.json"
        source.write_text('{"name":"Live"}', encoding="utf-8")
        jobs = [{"type": "material-descriptor", "source": source.name}]
        result = self.analyze(jobs)
        self.assertNotEqual(result.returncode, 0)
        report = json.loads((self.root / "analysis.json").read_text(encoding="utf-8"))
        self.assertFalse(report["complete"])
        entry = self.capture(source.name, '{"name":"Replacement"}')
        Path(entry["file"]["path"]).write_text('{"name":"Altered"}', encoding="utf-8")
        result = self.analyze(jobs)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(json.loads((self.root / "analysis.json").read_text(encoding="utf-8"))["complete"])

    def test_material_and_empty_scene_use_captures_with_absent_originals(self):
        self.capture("material.json", '{"name":"Stone"}')
        self.capture("scene.json", '{"version":9,"name":"Empty","nodes":[]}')
        result = self.batch([
            {"type": "material-descriptor", "source": "material.json"},
            {"type": "scene-descriptor", "source": "scene.json"},
        ])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(list((self.root / "cooked").rglob("*.omat")))
        self.assertTrue(list((self.root / "cooked").rglob("*.oscene")))
        self.assertFalse((self.root / "material.json").exists())

    def test_texture_descriptor_and_image_use_captured_bytes(self):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(b"\x00\xff\x00\x00\xff"))
        png += chunk(b"IEND", b"")
        self.capture("image.png", png)
        texture_path = "/.cooked/Textures/Red.otex"
        self.capture("texture.json", json.dumps({"name": "Red", "source": "image.png",
                     "virtual_path": texture_path, "mips": {"policy": "none"}}))
        self.capture("material.json", json.dumps({"name": "RedMaterial", "textures": {
            "base_color": {"virtual_path": texture_path},
        }}))
        result = self.batch([
            {"id": "texture", "type": "texture-descriptor", "source": "texture.json"},
            {"type": "material-descriptor", "source": "material.json", "depends_on": ["texture"]},
        ])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(list((self.root / "cooked").rglob("*.otex")))
        self.assertTrue(list((self.root / "cooked").rglob("*.omat")))

    def test_model_imports_keep_logical_paths(self):
        for extension in ("gltf", "fbx"):
            with self.subTest(extension=extension):
                name = f"static_scalar_triangle.{extension}"
                self.capture(name, (Path(__file__).parent / "Models" / name).read_bytes())
                result = self.batch([{
                    "type": extension, "source": name,
                    "material_slot_source_identity": "01990000-0000-7000-8000-000000000001",
                }])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse((self.root / name).exists())

    def test_geometry_and_scene_resolve_cooked_references_outside_capture_map(self):
        self.capture("material.json", '{"name":"Stone"}')
        vertices = b"".join(struct.pack("<8f", *position, 0, 0, 1, 0, 0)
                            for position in ((0, 0, 0), (1, 0, 0), (0, 1, 0)))
        self.capture("vertices.bin", vertices)
        self.capture("indices.bin", struct.pack("<3I", 0, 1, 2))
        bounds = {"min": [0, 0, 0], "max": [1, 1, 0]}
        vb = "/.cooked/Resources/Buffers/vertices.obuf"
        ib = "/.cooked/Resources/Buffers/indices.obuf"
        buffers = [{"uri": name, "virtual_path": path, "usage_flags": usage,
                    "element_stride": stride,
                    "views": [{"name": "surface", "element_offset": 0, "element_count": 3}]}
                   for name, path, usage, stride in (("vertices.bin", vb, 1, 32), ("indices.bin", ib, 2, 4))]
        geometry = {"name": "Triangle", "bounds": bounds, "buffers": buffers, "lods": [{
            "name": "LOD0", "mesh_type": "standard", "bounds": bounds,
            "buffers": {"vb_ref": vb, "ib_ref": ib}, "submeshes": [{
                "slot_id": "018f8f8f-1111-7111-8111-111111111111",
                "material_ref": "/.cooked/Materials/Stone.omat", "views": [{"view_ref": "surface"}],
            }],
        }]}
        self.capture("geometry.json", json.dumps(geometry))
        self.capture("scene.json", json.dumps({"version": 9, "name": "TriangleScene",
                     "nodes": [{"name": "Triangle"}], "renderables": [{
                         "node": 0, "geometry_ref": "/.cooked/Geometry/Triangle.ogeo",
                     }]}))
        result = self.batch([
            {"id": "material", "type": "material-descriptor", "source": "material.json"},
            {"id": "geometry", "type": "geometry-descriptor", "source": "geometry.json", "depends_on": ["material"]},
            {"type": "scene-descriptor", "source": "scene.json", "depends_on": ["geometry"]},
        ])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(list((self.root / "cooked").rglob("*.ogeo")))
        self.assertTrue(list((self.root / "cooked").rglob("*.oscene")))

    def test_undeclared_descriptor_cannot_fall_back_to_live_source(self):
        (self.root / "material.json").write_text('{"name":"Live"}', encoding="utf-8")
        result = self.batch([{"type": "material-descriptor", "source": "material.json"}])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not captured", result.stdout + result.stderr)

    def test_descriptor_digest_mismatch_fails_before_cooking(self):
        entry = self.capture("material.json", '{"name":"Stone"}')
        Path(entry["file"]["path"]).write_text('{"name":"Other"}', encoding="utf-8")
        result = self.batch([{"type": "material-descriptor", "source": "material.json"}])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("digest mismatch", result.stdout + result.stderr)
        self.assertFalse(list((self.root / "cooked").rglob("*.omat")))

    def test_dependency_preflight_cannot_read_an_undeclared_live_sidecar(self):
        self.capture("scene.json", '{"version":9,"name":"Empty","nodes":[]}')
        (self.root / "physics.json").write_text('{"bindings":{}}', encoding="utf-8")
        result = self.batch([
            {"id": "scene", "type": "scene-descriptor", "source": "scene.json"},
            {"id": "physics", "type": "physics-sidecar", "source": "physics.json",
             "target_scene_virtual_path": "/.cooked/Scenes/Empty.oscene"},
        ])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not captured", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
