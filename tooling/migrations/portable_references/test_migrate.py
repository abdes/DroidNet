"""Tests the migration's rules over in-memory project trees."""

from __future__ import annotations

import json
import unittest

from tooling.migrations.portable_references.migrate import (
    identity_of,
    load,
    migrate_tree,
    to_reference,
)


def project_file(*mounts: dict[str, str], version: int = 2) -> str:
    return json.dumps(
        {"SchemaVersion": version, "Name": "Game", "Mounts": list(mounts)}
    )


CONTENT = {"Name": "Content", "Kind": "Source", "Path": "Content"}
RAW = {"Name": "Raw", "Kind": "Source"}


def record(source: str) -> str:
    return json.dumps(
        {
            "schema_version": 1,
            "source": source,
            "virtual_path": source,
            "settings": {"name": "x"},
            "source_identity": "id",
        }
    )


class ReferenceSpellingTests(unittest.TestCase):
    def test_same_folder_is_the_name(self) -> None:
        self.assertEqual(
            to_reference("/Content/Cars/Truck.fbx", "/Content/Cars/Truck.fbx"),
            "Truck.fbx",
        )

    def test_same_mount_is_the_shortest_relative_path(self) -> None:
        self.assertEqual(
            to_reference("/Content/Scenes/Main.oscene", "/Content/Textures/Brick.otex"),
            "../Textures/Brick.otex",
        )

    def test_another_mount_is_absolute(self) -> None:
        self.assertEqual(
            to_reference(
                "/Content/Scenes/Main.oscene", "/Engine/Materials/Default.omat"
            ),
            "/Engine/Materials/Default.omat",
        )

    def test_a_descriptors_suffix_is_not_its_identity(self) -> None:
        self.assertEqual(
            identity_of("/Content/Textures/pisa.otex.json"),
            "/Content/Textures/pisa.otex",
        )


class ProjectFileTests(unittest.TestCase):
    def test_a_pak_mount_becomes_cooked_on_the_current_schema(self) -> None:
        pak = {"Name": "Props", "Kind": "Pak", "Path": "Props.pak"}
        migration = migrate_tree({"Project.oxy": project_file(CONTENT, pak)}, set())
        project = load(migration.writes["Project.oxy"])
        self.assertEqual(project["SchemaVersion"], 3)
        self.assertEqual(project["Mounts"][1]["Kind"], "Cooked")

    def test_a_mounts_file_states_the_one_version_key(self) -> None:
        tree = {
            "Project.oxy": project_file(CONTENT, RAW),
            ".mounts.json": json.dumps({"version": 1, "mounts": {"Raw": "D:/Raw"}}),
        }
        mounts = load(migrate_tree(tree, set()).writes[".mounts.json"])
        self.assertEqual(mounts, {"schema_version": 2, "mounts": {"Raw": "D:/Raw"}})


class RecordTests(unittest.TestCase):
    def test_a_record_moves_to_its_identity_without_virtual_path(self) -> None:
        tree = {
            "Project.oxy": project_file(CONTENT),
            "Imports/Content/Cars/Truck.fbx.import.json": record(
                "/Content/Cars/Truck.fbx"
            ),
        }
        migration = migrate_tree(tree, {"Content/Cars/Truck.fbx"})
        moved = load(migration.writes["Content/Cars/Truck.fbx.import.json"])
        self.assertNotIn("virtual_path", moved)
        self.assertEqual(moved["source"], "Truck.fbx")
        self.assertIn("Imports/Content/Cars/Truck.fbx.import.json", migration.deletes)
        self.assertEqual(migration.unresolved, [])

    def test_an_old_sidecar_at_the_records_path_is_replaced(self) -> None:
        tree = {
            "Project.oxy": project_file(CONTENT),
            "Content/Cars/Truck.fbx.import.json": json.dumps({"SchemaVersion": 6}),
            "Imports/Content/Cars/Truck.fbx.import.json": record(
                "/Content/Cars/Truck.fbx"
            ),
        }
        migration = migrate_tree(tree, {"Content/Cars/Truck.fbx"})
        self.assertEqual(
            load(migration.writes["Content/Cars/Truck.fbx.import.json"])[
                "schema_version"
            ],
            1,
        )
        self.assertNotIn("Content/Cars/Truck.fbx.import.json", migration.deletes)

    def test_a_legacy_material_sidecar_is_deleted(self) -> None:
        tree = {
            "Project.oxy": project_file(CONTENT),
            "Content/M.omat.json.import.json": json.dumps({"SchemaVersion": 1}),
        }
        self.assertIn(
            "Content/M.omat.json.import.json", migrate_tree(tree, set()).deletes
        )

    def test_a_record_that_cannot_be_placed_stays_and_is_reported(self) -> None:
        tree = {
            "Project.oxy": project_file(CONTENT),
            "Content/Cars/Truck.fbx.import.json": record("/Content/Cars/Truck.fbx"),
            "Imports/Content/Cars/Truck.fbx.import.json": record(
                "/Content/Cars/Truck.fbx"
            ),
        }
        migration = migrate_tree(tree, {"Content/Cars/Truck.fbx"})
        self.assertNotIn(
            "Imports/Content/Cars/Truck.fbx.import.json", migration.deletes
        )
        self.assertEqual(len(migration.unresolved), 1)

    def test_a_missing_source_is_reported(self) -> None:
        tree = {
            "Project.oxy": project_file(CONTENT),
            "Imports/Content/Cars/Truck.fbx.import.json": record(
                "/Content/Cars/Truck.fbx"
            ),
        }
        self.assertIn("is not in the project", migrate_tree(tree, set()).unresolved[0])

    def test_a_model_of_a_raw_mount_moves_into_an_own_mount_and_its_scene_follows(
        self,
    ) -> None:
        scene = json.dumps(
            {
                "Source": "asset:///Raw/Models/Truck.glb",
                "GeometryUri": "asset:///Raw/Models/Truck.glb/Hull.ogeo",
            }
        )
        tree = {
            "Project.oxy": project_file(CONTENT, RAW),
            "Imports/Raw/Models/Truck.glb.import.json": record("/Raw/Models/Truck.glb"),
            "Content/Scenes/Main.oscene.json": scene,
        }
        migration = migrate_tree(tree, set())
        moved = load(migration.writes["Content/Truck/Truck.glb.import.json"])
        self.assertEqual(moved["source"], "/Raw/Models/Truck.glb")
        written = load(migration.writes["Content/Scenes/Main.oscene.json"])
        self.assertEqual(
            written,
            {
                "Source": "../Truck/Truck.glb",
                "GeometryUri": "../Truck/Truck.glb/Hull.ogeo",
            },
        )


class DescriptorTests(unittest.TestCase):
    def test_a_texture_loses_its_identity_and_keeps_a_relative_source(self) -> None:
        text = json.dumps(
            {"name": "B", "virtual_path": "/Content/T/B.otex", "source": "B.png"}
        )
        migration = migrate_tree(
            {"Project.oxy": project_file(CONTENT), "Content/T/B.otex.json": text}, set()
        )
        self.assertEqual(
            load(migration.writes["Content/T/B.otex.json"]),
            {"name": "B", "source": "B.png"},
        )

    def test_a_material_respells_its_textures(self) -> None:
        text = json.dumps(
            {
                "textures": {
                    "normal": {"virtual_path": "/Content/T/N.otex"},
                    "orm": {"virtual_path": "/Engine/T/O.otex"},
                }
            }
        )
        migration = migrate_tree(
            {"Project.oxy": project_file(CONTENT), "Content/M/A.omat.json": text}, set()
        )
        textures = load(migration.writes["Content/M/A.omat.json"])["textures"]
        self.assertEqual(textures["normal"]["virtual_path"], "../T/N.otex")
        self.assertEqual(textures["orm"]["virtual_path"], "/Engine/T/O.otex")

    def test_a_file_whose_data_does_not_change_is_not_rewritten(self) -> None:
        migration = migrate_tree(
            {
                "Project.oxy": project_file(CONTENT),
                "Content/M/A.omat.json": '{"name":"A"}',
            },
            set(),
        )
        self.assertNotIn("Content/M/A.omat.json", migration.writes)


class SceneTests(unittest.TestCase):
    def test_uris_become_references_and_other_text_stays(self) -> None:
        scene = json.dumps(
            {
                "Name": "asset",
                "Cubemap": "asset:///Content/Textures/pisa.otex.json",
                "Geometry": "asset:///Engine/Generated/Cube.ogeo",
                "Path": "asset:///Content/My%20Folder/A.ogeo",
            }
        )
        migration = migrate_tree(
            {
                "Project.oxy": project_file(CONTENT),
                "Content/Scenes/Main.oscene.json": scene,
            },
            set(),
        )
        written = load(migration.writes["Content/Scenes/Main.oscene.json"])
        self.assertEqual(written["Name"], "asset")
        self.assertEqual(written["Cubemap"], "../Textures/pisa.otex")
        self.assertEqual(written["Geometry"], "/Engine/Generated/Cube.ogeo")
        self.assertEqual(written["Path"], "../My Folder/A.ogeo")

    def test_a_named_output_reference_becomes_its_authored_asset(self) -> None:
        scene = json.dumps({"Scripts": ["asset:///.cooked/Scripts/rotate.oscript"]})
        migration = migrate_tree(
            {
                "Project.oxy": project_file(CONTENT),
                "Content/Scenes/Main.oscene.json": scene,
            },
            set(),
        )
        self.assertEqual(
            load(migration.writes["Content/Scenes/Main.oscene.json"])["Scripts"],
            ["/Showcase/Scenes/RotatingGltf/rotate.oscript"],
        )
        self.assertEqual(migration.unresolved, [])

    def test_any_other_output_reference_is_reported(self) -> None:
        scene = json.dumps({"Value": "asset:///.cooked/Gone/A.omat"})
        migration = migrate_tree(
            {
                "Project.oxy": project_file(CONTENT),
                "Content/Scenes/Main.oscene.json": scene,
            },
            set(),
        )
        self.assertIn("names an output folder", migration.unresolved[0])


if __name__ == "__main__":
    unittest.main()
