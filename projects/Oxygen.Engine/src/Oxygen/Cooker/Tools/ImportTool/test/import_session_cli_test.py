"""Run an import manifest shaped like an editor import session through the CLI.

An editor import session is an import manifest: one model job per file, in the
dialog's order, with a source relative to the manifest, the model's identity,
its name and only the settings the user set; the run's execution settings at
the top when set; no output, as the command line names the cooked root.
"""

import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

IMPORT_TOOL = Path(sys.argv.pop(1)).resolve()
MODELS = Path(__file__).resolve().parents[3] / "test" / "Assets" / "Models"

POSITIONS = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)


def write_gltf(directory: Path, name: str) -> Path:
    """A one-triangle glTF whose positions live in a buffer file beside it."""
    directory.mkdir(parents=True, exist_ok=True)
    buffer = directory / "positions.bin"
    buffer.write_bytes(POSITIONS)
    source = directory / name
    source.write_text(
        json.dumps(
            {
                "asset": {"version": "2.0"},
                "buffers": [{"uri": buffer.name, "byteLength": len(POSITIONS)}],
                "bufferViews": [{"buffer": 0, "byteLength": len(POSITIONS)}],
                "accessors": [
                    {
                        "bufferView": 0,
                        "componentType": 5126,
                        "count": 3,
                        "type": "VEC3",
                        "min": [0, 0, 0],
                        "max": [1, 1, 0],
                    }
                ],
                "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
                "nodes": [{"name": "Root", "mesh": 0}],
                "scenes": [{"nodes": [0]}],
                "scene": 0,
            }
        ),
        encoding="utf-8",
    )
    return source


class ImportSessionCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="oxygen-session-cli-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def test_editor_session_runs_as_a_batch(self):
        session_dir = self.root / "Project"
        write_gltf(session_dir / "Originals" / "Ship", "Hull.gltf")
        fbx_dir = session_dir / "Originals" / "Props"
        fbx_dir.mkdir(parents=True)
        shutil.copy(MODELS / "static_scalar_triangle.fbx", fbx_dir / "Crate.fbx")
        session = session_dir / "ships.import.json"
        session.write_text(
            json.dumps(
                {
                    "version": 1,
                    "thread_pool_size": 2,
                    "max_in_flight_jobs": 2,
                    "concurrency": {
                        "texture": {"workers": 1},
                        "scene": {"queue_capacity": 4},
                    },
                    "layout": {"virtual_mount_root": "/Content"},
                    "jobs": [
                        {
                            "type": "gltf",
                            "source": "Originals/Ship/Hull.gltf",
                            "virtual_path": "/Content/Models/Hull.gltf",
                            "name": "Hull",
                            "gltf_scene": "#0",
                        },
                        {
                            "type": "fbx",
                            "source": "Originals/Props/Crate.fbx",
                            "virtual_path": "/Content/Models/Crate.fbx",
                            "name": "Crate",
                            "import_cameras": False,
                        },
                    ],
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        cooked_root = self.root / "Cooked"
        report_path = self.root / "report.json"

        result = subprocess.run(
            [
                str(IMPORT_TOOL),
                "--no-tui",
                "batch",
                "--manifest",
                str(session),
                "--cooked-root",
                str(cooked_root),
                "--report",
                str(report_path),
            ],
            capture_output=True,
            text=True,
            timeout=120,
            check=False,
        )
        self.assertEqual(result.returncode, 0, f"{result.stdout}\n{result.stderr}")

        report = json.loads(report_path.read_text(encoding="utf-8"))
        jobs = report["jobs"]
        self.assertEqual(len(jobs), 2)
        for job, identity in zip(
            jobs,
            ["/Content/Models/Hull.gltf", "/Content/Models/Crate.fbx"],
            strict=True,
        ):
            with self.subTest(identity=identity):
                self.assertEqual(job["status"], "succeeded", json.dumps(job))
                products = job["products"]
                self.assertTrue(products)
                for product in products:
                    self.assertTrue(
                        product["virtual_path"].startswith(identity + "/"),
                        product["virtual_path"],
                    )
                    self.assertTrue(
                        (cooked_root / product["descriptor_relative_path"]).is_file(),
                        product["descriptor_relative_path"],
                    )


if __name__ == "__main__":
    unittest.main()
