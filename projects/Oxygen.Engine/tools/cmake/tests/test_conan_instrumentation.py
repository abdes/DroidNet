"""Opt-in graph checks against locally provisioned Oxygen dependency recipes."""

import json
import os
from pathlib import Path
import tempfile
import unittest

from test_build_contract import CONAN, ENGINE, CommandTests


@unittest.skipUnless(CONAN and os.environ.get("OXYGEN_RUN_CONAN_GRAPH_INTEGRATION"),
                     "Opt in with OXYGEN_RUN_CONAN_GRAPH_INTEGRATION=1 and cached recipes")
class ConanInstrumentationTests(CommandTests):
    def graph(self, profile, *extra):
        result = self.run_command([
            CONAN, "graph", "info", str(ENGINE), "-pr:h", str(profile),
            "-pr:b", str(ENGINE / "profiles/windows-msvc.ini"),
            "-s:h", "build_type=Debug", "-s:b", "build_type=Release",
            "-o", "&:tests=True",
            "--no-remote", "--format=json", *extra,
        ], ENGINE)
        return json.loads(result.stdout)["graph"]

    def test_profile_identity_matches_wrapper_and_inherited_profile(self):
        graphs = {}
        for mode, filename in (("ordinary", "windows-msvc.ini"), ("asan", "windows-msvc-asan.ini")):
            profile = ENGINE / "profiles" / filename
            direct = self.graph(profile)
            wrapper = self.graph(profile, "-o", "with_tracy=False",
                                 "-c", "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config")
            host = lambda graph: {node["name"]: node for node in graph["nodes"].values()
                                  if node["context"] == "host" and node["name"] != "oxygen"}
            graphs[mode] = host(direct)
            self.assertEqual({name: n["package_id"] for name, n in host(direct).items()},
                             {name: n["package_id"] for name, n in host(wrapper).items()})
            with tempfile.TemporaryDirectory() as tmp:
                inherited = Path(tmp) / "profile.ini"
                inherited.write_text(f"include({profile.as_posix()})\n", encoding="utf-8")
                resolved = self.run_command([CONAN, "profile", "show", "-pr:h", str(inherited),
                                             "-pr:b", str(profile), "--format=json"], ENGINE)
                self.assertEqual(json.loads(resolved.stdout)["host"]["settings"]["sanitizer"],
                                 "asan" if mode == "asan" else "None")
                self.assertEqual({name: n["package_id"] for name, n in host(self.graph(inherited)).items()},
                                 {name: n["package_id"] for name, n in host(direct).items()})
        for name in ("sdl", "imgui", "joltphysics", "gtest", "tinyexr", "xxhash"):
            self.assertNotEqual(graphs["ordinary"][name]["package_id"], graphs["asan"][name]["package_id"], name)
        self.assertEqual(graphs["ordinary"]["fmt"]["package_id"], graphs["asan"]["fmt"]["package_id"])
        self.assertEqual(graphs["ordinary"]["tinyexr"]["options"]["with_openmp"], "True")
        self.assertEqual(graphs["asan"]["tinyexr"]["options"]["with_openmp"], "False")
        self.assertEqual(graphs["asan"]["tinyexr"]["options"]["with_thread"], "True")

    def test_option_alone_cannot_instrument_an_ordinary_graph(self):
        graph = self.graph(ENGINE / "profiles/windows-msvc.ini", "-o", "with_asan=True")
        root = graph["nodes"]["0"]
        self.assertIn("ASan requires the ASan host profile", root["info_invalid"])


if __name__ == "__main__":
    unittest.main()
