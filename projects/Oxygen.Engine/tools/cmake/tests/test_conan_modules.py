# pyright: strict
"""Checks of the real selected Conan graph and canonical binary identity."""

import unittest

from cmake_testing import PROFILES, ConanGraphTestCase, GraphNodes, Json, options, requires_conan, requires_env


def dependency_names(nodes: GraphNodes) -> set[str]:
    return {node["name"] for key, node in nodes.items() if key != "0"}


def node_named(nodes: GraphNodes, name: str) -> Json:
    return next(node for node in nodes.values() if node["name"] == name)


@requires_conan
@requires_env("OXYGEN_RUN_CONAN_GRAPH_INTEGRATION", "Needs the dependency recipes in the local Conan cache")
class ModuleGraphTests(ConanGraphTestCase):
    def assert_valid(self, nodes: GraphNodes) -> None:
        self.assertIsNone(nodes["0"].get("info_invalid"))

    def test_exact_external_closures(self) -> None:
        for selection, expected in (
            ("Base", {"fmt"}),
            ("OxCo", {"fmt", "asio"}),
            ("Clap", {"fmt", "magic_enum"}),
            ("OxCo,Clap", {"fmt", "asio", "magic_enum"}),
        ):
            with self.subTest(selection=selection):
                nodes = self.graph(*options(f"&:modules={selection}"))
                self.assert_valid(nodes)
                self.assertEqual(dependency_names(nodes), expected)
                self.assertFalse(any(node["context"] == "build" for node in nodes.values()))

    def test_equivalent_selections_and_checker_defaults_have_equal_ids(self) -> None:
        variants = {
            self.graph(*options(*selection))["0"]["package_id"]
            for selection in (
                ("&:modules=OxCo,Clap",),
                ("&:modules=Clap,Base,OxCo,Clap",),
                ("&:modules=Base,OxCo,TextWrap,Clap",),
                ("&:modules=OxCo,Clap", "&:awaitable_state_checker=True"),
            )
        }
        self.assertEqual(len(variants), 1)
        disabled = self.graph(*options("&:modules=OxCo,Clap", "&:awaitable_state_checker=False"))
        self.assertNotIn(disabled["0"]["package_id"], variants)

    def test_consumer_defaults_and_explicit_development_requirements(self) -> None:
        full = self.graph()
        root_options = full["0"]["options"]
        self.assertEqual(root_options["modules"], "full")
        self.assertEqual(root_options["shared"], "True")
        for name in ("tests", "benchmarks", "examples", "docs", "tools"):
            self.assertEqual(root_options[name], "False", name)
        self.assertFalse({"gtest", "benchmark"} & dependency_names(full))
        self.assertEqual({node["context"] for node in full.values() if node["name"] == "dxc"}, {"host", "build"})
        development = self.graph(*options("&:modules=OxCo", "&:tests=True", "&:benchmarks=True"))
        self.assertLessEqual({"gtest", "benchmark"}, dependency_names(development))

    def test_tracy_dependency_is_present_only_when_enabled(self) -> None:
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                nodes = self.graph(*options(f"&:with_tracy={enabled}"))
                self.assert_valid(nodes)
                tracy = [node for node in nodes.values() if node["name"] == "tracy"]
                self.assertEqual(len(tracy), int(enabled))
                if tracy:
                    self.assertEqual(tracy[0]["options"]["enable"], "True")

    def test_ui_instrumentation_selects_matching_imgui_and_package_identity(self) -> None:
        ordinary = self.graph(*options("&:examples=True"))
        instrumented = self.graph(*options("&:examples=True", "&:ui_tests=True"))
        for nodes, enabled in ((ordinary, "False"), (instrumented, "True")):
            self.assert_valid(nodes)
            self.assertEqual(node_named(nodes, "imgui")["options"]["enable_test_engine"], enabled)
        self.assertNotEqual(ordinary["0"]["package_id"], instrumented["0"]["package_id"])
        for selection in (("&:examples=False",), ("&:modules=OxCo", "&:examples=True")):
            with self.subTest(selection=selection):
                nodes = self.graph(*options("&:ui_tests=True", *selection))
                self.assertIn("UI tests require", nodes["0"]["info_invalid"])

    def test_local_static_graphs_remain_valid(self) -> None:
        full = self.graph(*options("&:shared=False"))["0"]
        self.assertIsNone(full.get("info_invalid"))
        self.assertFalse(full.get("invalid_build"))
        module = self.graph(*options("&:modules=Base", "&:shared=False"))["0"]
        self.assertFalse(module.get("invalid_build"))

    def test_portable_modules_do_not_inherit_full_engine_platform_restrictions(self) -> None:
        for profile in ("Linux-gcc.ini", "macos-clang.ini"):
            with self.subTest(profile=profile):
                nodes = self.graph(*options("&:modules=OxCo,Clap"), profile=PROFILES / profile)
                self.assert_valid(nodes)
                self.assertEqual(dependency_names(nodes), {"fmt", "asio", "magic_enum"})


if __name__ == "__main__":
    unittest.main()
