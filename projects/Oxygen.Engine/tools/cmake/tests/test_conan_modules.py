"""Opt-in checks of the real selected Conan graph and canonical binary identity."""
import json
import os
import unittest

from test_build_contract import CONAN, ENGINE, CommandTests


@unittest.skipUnless(CONAN and os.environ.get("OXYGEN_RUN_CONAN_GRAPH_INTEGRATION"),
                     "Requires the cached dependency recipes")
class ModuleGraphTests(CommandTests):
    def graph(self, *options, profile="windows-msvc.ini"):
        result = self.run_command([
            CONAN, "graph", "info", str(ENGINE),
            "-pr:h", str(ENGINE / "profiles" / profile),
            "-pr:b", str(ENGINE / "profiles/windows-msvc.ini"),
            "-s:h", "build_type=Debug", "-s:b", "build_type=Release",
            "--no-remote", "--format=json", *options,
        ], ENGINE)
        return json.loads(result.stdout)["graph"]["nodes"]

    def test_exact_external_closures(self):
        for selection, expected in (
            ("Base", {"fmt"}), ("OxCo", {"fmt", "asio"}),
            ("Clap", {"fmt", "magic_enum"}),
            ("OxCo,Clap", {"fmt", "asio", "magic_enum"}),
        ):
            with self.subTest(selection=selection):
                nodes = self.graph("-o", f"&:modules={selection}")
                self.assertIsNone(nodes["0"].get("info_invalid"))
                self.assertEqual({node["name"] for key, node in nodes.items() if key != "0"}, expected)
                self.assertFalse(any(node["context"] == "build" for node in nodes.values()))

    def test_equivalent_selections_and_checker_defaults_have_equal_ids(self):
        variants = []
        for selection in ("OxCo,Clap", "Clap,Base,OxCo,Clap", "Base,OxCo,TextWrap,Clap"):
            variants.append(self.graph("-o", f"&:modules={selection}")["0"]["package_id"])
        variants.append(self.graph("-o", "&:modules=OxCo,Clap", "-o", "&:awaitable_state_checker=True")["0"]["package_id"])
        self.assertEqual(len(set(variants)), 1)
        disabled = self.graph("-o", "&:modules=OxCo,Clap", "-o", "&:awaitable_state_checker=False")
        self.assertNotEqual(variants[0], disabled["0"]["package_id"])

    def test_consumer_defaults_and_explicit_development_requirements(self):
        full = self.graph()
        self.assertEqual(full["0"]["options"]["modules"], "full")
        self.assertEqual(full["0"]["options"]["shared"], "True")
        for name in ("tests", "benchmarks", "examples", "docs", "tools"):
            self.assertEqual(full["0"]["options"][name], "False")
        self.assertFalse({"gtest", "benchmark"}.intersection(node["name"] for node in full.values()))
        self.assertEqual({node["context"] for node in full.values() if node["name"] == "dxc"}, {"host", "build"})
        development = self.graph("-o", "&:modules=OxCo", "-o", "&:tests=True", "-o", "&:benchmarks=True")
        self.assertTrue({"gtest", "benchmark"}.issubset(node["name"] for node in development.values()))

    def test_tracy_dependency_is_present_only_when_enabled(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                nodes = self.graph("-o", f"&:with_tracy={enabled}")
                self.assertIsNone(nodes["0"].get("info_invalid"))
                tracy = [node for node in nodes.values() if node["name"] == "tracy"]
                self.assertEqual(len(tracy), int(enabled))
                if tracy:
                    self.assertEqual(tracy[0]["options"]["enable"], "True")

    def test_ui_instrumentation_selects_matching_imgui_and_package_identity(self):
        ordinary = self.graph("-o", "&:examples=True")
        instrumented = self.graph("-o", "&:examples=True", "-o", "&:ui_tests=True")
        for nodes, enabled in ((ordinary, "False"), (instrumented, "True")):
            self.assertIsNone(nodes["0"].get("info_invalid"))
            imgui = next(node for node in nodes.values() if node["name"] == "imgui")
            self.assertEqual(imgui["options"]["enable_test_engine"], enabled)
        self.assertNotEqual(ordinary["0"]["package_id"], instrumented["0"]["package_id"])
        for options in (("&:examples=False",), ("&:modules=OxCo", "&:examples=True")):
            arguments = [argument for option in options for argument in ("-o", option)]
            nodes = self.graph("-o", "&:ui_tests=True", *arguments)
            self.assertIn("UI tests require", nodes["0"]["info_invalid"])

    def test_local_static_graphs_remain_valid(self):
        full = self.graph("-o", "&:shared=False")["0"]
        self.assertIsNone(full.get("info_invalid"))
        self.assertFalse(full.get("invalid_build"))
        module = self.graph("-o", "&:modules=Base", "-o", "&:shared=False")["0"]
        self.assertFalse(module.get("invalid_build"))

    def test_portable_modules_do_not_inherit_full_engine_platform_restrictions(self):
        for profile in ("Linux-gcc.ini", "macos-clang.ini"):
            with self.subTest(profile=profile):
                nodes = self.graph("-o", "&:modules=OxCo,Clap", profile=profile)
                self.assertIsNone(nodes["0"].get("info_invalid"))
                self.assertEqual({node["name"] for key, node in nodes.items() if key != "0"}, {"fmt", "asio", "magic_enum"})


if __name__ == "__main__":
    unittest.main()
