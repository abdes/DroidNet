# pyright: strict
"""Graph checks against locally provisioned Oxygen dependency recipes."""

import json
import unittest
from pathlib import Path

from cmake_testing import (
    CONAN,
    ENGINE,
    PROFILES,
    WINDOWS_PROFILE,
    ConanGraphTestCase,
    GraphNodes,
    requires_conan,
    requires_env,
    write,
)

INSTRUMENTED = ("sdl", "imgui", "joltphysics", "gtest", "tinyexr", "xxhash")


def host_ids(nodes: GraphNodes) -> dict[str, str]:
    """Package ids of the host dependencies, excluding the engine itself."""
    return {node["name"]: node["package_id"] for node in host_nodes(nodes).values()}


def host_nodes(nodes: GraphNodes) -> GraphNodes:
    return {node["name"]: node for node in nodes.values() if node["context"] == "host" and node["name"] != "oxygen"}


@requires_conan
@requires_env("OXYGEN_RUN_CONAN_GRAPH_INTEGRATION", "Needs the dependency recipes in the local Conan cache")
class ConanInstrumentationTests(ConanGraphTestCase):
    def graph_with_tests(self, profile: Path, *arguments: str) -> GraphNodes:
        return self.graph("-o", "&:tests=True", *arguments, profile=profile)

    def test_profile_identity_matches_wrapper_and_inherited_profile(self) -> None:
        graphs: dict[str, GraphNodes] = {}
        for mode, filename in (("ordinary", "windows-msvc.ini"), ("asan", "windows-msvc-asan.ini")):
            with self.subTest(mode=mode):
                profile = PROFILES / filename
                direct = self.graph_with_tests(profile)
                graphs[mode] = host_nodes(direct)
                wrapper = self.graph_with_tests(
                    profile, "-o", "with_tracy=False", "-c", "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config"
                )
                self.assertEqual(host_ids(direct), host_ids(wrapper))
                inherited = write(self.root / f"{mode}-profile.ini", f"include({profile.as_posix()})\n")
                resolved = self.run_command(
                    [CONAN, "profile", "show", "-pr:h", inherited, "-pr:b", profile, "--format=json"], cwd=ENGINE
                )
                self.assertEqual(
                    json.loads(resolved.stdout)["host"]["settings"]["sanitizer"], "asan" if mode == "asan" else "None"
                )
                self.assertEqual(host_ids(self.graph_with_tests(inherited)), host_ids(direct))
        ordinary, asan = graphs["ordinary"], graphs["asan"]
        for name in INSTRUMENTED:
            self.assertNotEqual(ordinary[name]["package_id"], asan[name]["package_id"], name)
        self.assertEqual(ordinary["fmt"]["package_id"], asan["fmt"]["package_id"])
        self.assertEqual(ordinary["tinyexr"]["options"]["with_openmp"], "True")
        self.assertEqual(asan["tinyexr"]["options"]["with_openmp"], "False")
        self.assertEqual(asan["tinyexr"]["options"]["with_thread"], "True")

    def test_option_alone_cannot_instrument_an_ordinary_graph(self) -> None:
        root = self.graph_with_tests(WINDOWS_PROFILE, "-o", "with_asan=True")["0"]
        self.assertIn("ASan requires the ASan host profile", root["info_invalid"])


if __name__ == "__main__":
    unittest.main()
