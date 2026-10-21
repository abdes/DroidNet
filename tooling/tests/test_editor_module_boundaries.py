# pyright: strict
"""The editor's module boundaries, read from the project files.

Checks the layer rules of section 5.2 and the internals rule of decision 4 of
design/editor/lld/editor-module-architecture.md: which production projects may
reference which, and that a project grants its internals only to its own test
projects.
"""

from __future__ import annotations

import re
import unittest
from dataclasses import dataclass
from enum import Enum
from functools import cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECTS = ROOT / "projects"

# The engine is a separate CMake project with no managed project files.
EXCLUDED = {"Oxygen.Engine"}

# Moq's proxies are generated into this assembly; the grant lets a module's own
# tests mock its internal types.
MOCK_PROXIES = "DynamicProxyGenAssembly2"


class Layer(Enum):
    ROOT = "composition root"
    SHELL = "shell"
    FEATURE = "feature UI"
    SHARED_UI = "shared feature UI"
    DOMAIN = "domain"
    UI_KIT = "UI kit"
    FOUNDATION = "foundation"


# The layers each layer may reference (section 5.2). Domain modules also obey
# DOMAIN_RANK, and only ENGINE_USERS reference the engine.
ALLOWED: dict[Layer, frozenset[Layer]] = {
    Layer.ROOT: frozenset(Layer),
    Layer.SHELL: frozenset({Layer.FOUNDATION}),
    Layer.FEATURE: frozenset(
        {Layer.SHELL, Layer.SHARED_UI, Layer.DOMAIN, Layer.UI_KIT, Layer.FOUNDATION}
    ),
    Layer.SHARED_UI: frozenset({Layer.DOMAIN, Layer.UI_KIT, Layer.FOUNDATION}),
    Layer.DOMAIN: frozenset({Layer.DOMAIN, Layer.FOUNDATION}),
    Layer.UI_KIT: frozenset({Layer.FOUNDATION}),
    Layer.FOUNDATION: frozenset({Layer.FOUNDATION}),
}

# A domain module references only domain modules of a lower rank (section 5.4).
DOMAIN_RANK: dict[str, int] = {
    "Oxygen.Editor.Projects": 0,
    "Oxygen.Editor.Scripting": 1,
    "Oxygen.Editor.Input": 1,
    "Oxygen.Editor.Textures": 1,
    "Oxygen.Editor.ContentPipeline": 2,
    "Oxygen.Editor.Assets": 3,
    "Oxygen.Editor.Materials": 3,
    "Oxygen.Editor.Scenes": 3,
    "Oxygen.Editor.LivePreview": 4,
}

# Every Oxygen production project has a layer; DroidNet libraries are foundation.
EDITOR_LAYERS: dict[str, Layer] = {
    "Oxygen.Editor.App": Layer.ROOT,
    "Oxygen.Editor.Shell": Layer.SHELL,
    "Oxygen.Editor.ContentExplorer": Layer.FEATURE,
    "Oxygen.Editor.CookedInspector": Layer.FEATURE,
    "Oxygen.Editor.Cooking": Layer.FEATURE,
    "Oxygen.Editor.MaterialEditor": Layer.FEATURE,
    "Oxygen.Editor.Output": Layer.FEATURE,
    "Oxygen.Editor.Problems": Layer.FEATURE,
    "Oxygen.Editor.ProjectBrowser": Layer.FEATURE,
    "Oxygen.Editor.SceneEditor": Layer.FEATURE,
    "Oxygen.Editor.SceneExplorer": Layer.FEATURE,
    "Oxygen.Editor.SceneInspector": Layer.FEATURE,
    "Oxygen.Editor.WorldEditor": Layer.FEATURE,
    "Oxygen.Editor.Pickers": Layer.SHARED_UI,
    **{name: Layer.DOMAIN for name in DOMAIN_RANK},
    "Oxygen.Editor.Controls": Layer.UI_KIT,
    "Oxygen.Editor.Routing": Layer.FOUNDATION,
    "Oxygen.Editor.World": Layer.FOUNDATION,
    "Oxygen.Editor.Documents": Layer.FOUNDATION,
    "Oxygen.Editor.Schemas": Layer.FOUNDATION,
    "Oxygen.Editor.Data": Layer.FOUNDATION,
    "Oxygen.Editor.Data.Generators": Layer.FOUNDATION,
    "Oxygen.Editor.Data.Generators.Attributes": Layer.FOUNDATION,
    "Oxygen.Editor.Runtime": Layer.FOUNDATION,
    "Oxygen.Editor.Interop": Layer.FOUNDATION,
    "Oxygen.Managed.Core": Layer.FOUNDATION,
    "Oxygen.Managed.Core.Generators": Layer.FOUNDATION,
    "Oxygen.Managed.Assets": Layer.FOUNDATION,
}

# The engine's managed side. LivePreview is the only module that references it
# (sections 5.4 and 11); the composition root references everything.
ENGINE = frozenset({"Oxygen.Editor.Runtime", "Oxygen.Editor.Interop"})
ENGINE_USERS = frozenset({"Oxygen.Editor.LivePreview", "Oxygen.Editor.App"}) | ENGINE

# The one exception: the scene editor's viewports draw through Runtime's
# surfaces, views and input, and never reach Interop (section 11).
ENGINE_EXCEPTIONS: dict[str, frozenset[str]] = {
    "Oxygen.Editor.SceneEditor": frozenset({"Oxygen.Editor.Runtime"}),
}

# Violations the tree still has, each with the work that removes it. An entry
# that no longer occurs fails the test, so the lists only shrink.
DISSOLVE_WORLD_EDITOR = "#69 S1.13 dissolves the World Editor"
TEST_BOUNDARIES = "#69 S1.14 removes cross-module internals grants"
PENDING_REFERENCES: dict[tuple[str, str], str] = {
    ("Oxygen.Editor.WorldEditor", "Oxygen.Editor.Runtime"): DISSOLVE_WORLD_EDITOR,
    ("Oxygen.Editor.WorldEditor", "Oxygen.Editor.Interop"): DISSOLVE_WORLD_EDITOR,
}
PENDING_GRANTS: dict[tuple[str, str], str] = {
    ("Oxygen.Editor.World", "Oxygen.Editor.WorldEditor"): DISSOLVE_WORLD_EDITOR,
    **{
        ("Oxygen.Editor.World", grantee): TEST_BOUNDARIES
        for grantee in (
            "Oxygen.Editor.WorldEditor.Unit.Tests",
            "Oxygen.Editor.WorldEditor.Unit.UI.Tests",
            "Oxygen.Editor.ContentPipeline.Unit.Tests",
            "Oxygen.Editor.ContentPipeline.Integration.Tests",
            "Oxygen.Editor.ContentPipeline.Benchmarks.Tests",
        )
    },
    **{
        ("Oxygen.Editor.ContentPipeline", grantee): TEST_BOUNDARIES
        for grantee in (
            "Oxygen.Editor.WorldEditor.Unit.Tests",
            "Oxygen.Editor.WorldEditor.Unit.UI.Tests",
            "Oxygen.Editor.WorldEditor.Integration.UI.Tests",
            "Oxygen.Editor.WorldEditor.Benchmarks.UI.Tests",
            "Oxygen.Editor.MaterialEditor.Tests",
            "Oxygen.Editor.ContentExplorer.UI.Tests",
        )
    },
    **{
        ("Controls.InPlaceEdit", grantee): TEST_BOUNDARIES
        for grantee in ("Oxygen.Editor.SceneInspector.Unit.UI.Tests",)
    },
}

PROJECT_REFERENCE = re.compile(r'<ProjectReference\s+Include="([^"]+)"')
PROPERTY = re.compile(r"<(AssemblyName|RootNamespace)>([^<]+)</\1>", re.IGNORECASE)
GRANT_ITEM = re.compile(r'<InternalsVisibleTo\s+Include="([^"]+)"')
GRANT_ATTRIBUTE = re.compile(
    r'\[assembly:\s*(?:System\.Runtime\.CompilerServices\.)?InternalsVisibleTo\(\s*"([^"]+)"'
)
BUILD_OUTPUT = {"bin", "obj"}


@dataclass(frozen=True)
class Project:
    """A project file under projects/, as far as its boundaries need it."""

    path: Path

    @property
    def name(self) -> str:
        return self.path.stem

    @property
    def text(self) -> str:
        return self.path.read_text(encoding="utf-8-sig")

    @property
    def is_production(self) -> bool:
        """Production projects live in a module's src folder; tests, kits and samples do not."""
        return "src" in self.path.relative_to(PROJECTS).parts[:-1]

    @property
    def module(self) -> Path:
        """The folder of the module the project belongs to: the one that holds its src folder, if any."""
        parts = self.path.relative_to(PROJECTS).parts[:-1]
        return (
            PROJECTS.joinpath(*parts[: parts.index("src")])
            if "src" in parts
            else self.path.parent
        )

    @property
    def assembly(self) -> str:
        """The assembly name, as Directory.Build.props and the project file set it."""
        # MSBuild property names are case-insensitive.
        values = {
            "msbuildprojectname": self.name,
            "rootnamespace": "DroidNet",
            "assemblyname": f"DroidNet.{self.name}",
        }
        for key, value in PROPERTY.findall(self.text):
            values[key.lower()] = re.sub(
                r"\$\((\w+)\)",
                lambda match: values[match.group(1).lower()],
                value.strip(),
            )
        return values["assemblyname"]

    @property
    def references(self) -> list[str]:
        """The names of the projects it references."""
        return [
            Path(include.replace("\\", "/")).stem
            for include in PROJECT_REFERENCE.findall(self.text)
        ]

    @property
    def grants(self) -> set[str]:
        """The assemblies it grants its internals to, in the project file or its sources."""
        granted = set(GRANT_ITEM.findall(self.text))
        for source in self.path.parent.rglob("*"):
            if source.suffix in {".cs", ".cpp"} and BUILD_OUTPUT.isdisjoint(
                source.relative_to(self.path.parent).parts
            ):
                granted |= set(
                    GRANT_ATTRIBUTE.findall(
                        source.read_text(encoding="utf-8-sig", errors="replace")
                    )
                )
        return {grantee.split(",")[0].strip() for grantee in granted}


@cache
def projects() -> tuple[Project, ...]:
    found: list[Project] = []
    for pattern in ("*.csproj", "*.vcxproj"):
        for path in PROJECTS.rglob(pattern):
            parts = path.relative_to(PROJECTS).parts
            if parts[0] not in EXCLUDED and BUILD_OUTPUT.isdisjoint(parts):
                found.append(Project(path))
    return tuple(sorted(found, key=lambda project: project.path))


@cache
def production() -> dict[str, Project]:
    return {project.name: project for project in projects() if project.is_production}


def layer_of(name: str) -> Layer:
    if name.startswith("Oxygen."):
        return EDITOR_LAYERS[name]
    return Layer.FOUNDATION


def reference_violation(source: str, target: str) -> str | None:
    """Why source may not reference target, or None."""
    source_layer, target_layer = layer_of(source), layer_of(target)
    if target_layer is Layer.ROOT:
        return "nothing references the composition root"
    if (
        target in ENGINE
        and source not in ENGINE_USERS
        and target not in ENGINE_EXCEPTIONS.get(source, frozenset())
    ):
        return "only LivePreview references the engine"
    if target_layer not in ALLOWED[source_layer]:
        return f"{source_layer.value} does not reference {target_layer.value}"
    if (
        source_layer is Layer.DOMAIN
        and target_layer is Layer.DOMAIN
        and DOMAIN_RANK[target] >= DOMAIN_RANK[source]
    ):
        return "a domain module references only domain modules below it"
    return None


def grant_violation(
    grantor: Project, grantee: str, builders: dict[str, Project]
) -> str | None:
    """Why grantor may not grant its internals to grantee, or None."""
    if grantee == MOCK_PROXIES:
        return None
    target = builders.get(grantee)
    if target is None:
        return "no project builds that assembly"
    if target == grantor:
        return "a project does not grant its internals to itself"
    # The module's tests: its projects outside src (tests, kits, test hosts and probes), but not its samples.
    own = not target.is_production and target.path.is_relative_to(grantor.module)
    if not own or "samples" in target.path.relative_to(grantor.module).parts:
        return "internals are visible only to the module's own tests"
    return None


class EditorModuleBoundaries(unittest.TestCase):
    maxDiff = None

    def test_every_editor_project_has_a_layer(self) -> None:
        unplaced = sorted(
            name
            for name in production()
            if name.startswith("Oxygen.") and name not in EDITOR_LAYERS
        )
        self.assertEqual(
            unplaced,
            [],
            "place each new editor project in a layer of architecture section 5.2",
        )
        stale = sorted(name for name in EDITOR_LAYERS if name not in production())
        self.assertEqual(stale, [], "these layered projects no longer exist")

    def test_production_projects_reference_only_production_projects(self) -> None:
        found = [
            f"{project.name} -> {target}"
            for project in production().values()
            for target in project.references
            if target not in production()
        ]
        self.assertEqual(
            found, [], "test kits and support projects are referenced by tests only"
        )

    def test_references_point_down_the_layers(self) -> None:
        found: dict[tuple[str, str], str] = {}
        for project in production().values():
            for target in project.references:
                reason = (
                    reference_violation(project.name, target)
                    if target in production()
                    else None
                )
                if reason is not None:
                    found[project.name, target] = reason
        self.assertEqual(
            sorted(
                f"{source} -> {target}: {reason}"
                for (source, target), reason in found.items()
                if (source, target) not in PENDING_REFERENCES
            ),
            [],
        )
        self.assertEqual(
            sorted(
                f"{source} -> {target}"
                for source, target in PENDING_REFERENCES.keys() - found.keys()
            ),
            [],
            "remove resolved entries from PENDING_REFERENCES",
        )

    def test_internals_are_visible_only_to_the_module_tests(self) -> None:
        builders = {project.assembly: project for project in projects()}
        found: dict[tuple[str, str], str] = {}
        for project in production().values():
            for grantee in sorted(project.grants):
                reason = grant_violation(project, grantee, builders)
                if reason is not None:
                    found[project.name, grantee] = reason
        self.assertEqual(
            sorted(
                f"{grantor} -> {grantee}: {reason}"
                for (grantor, grantee), reason in found.items()
                if (grantor, grantee) not in PENDING_GRANTS
            ),
            [],
        )
        self.assertEqual(
            sorted(
                f"{grantor} -> {grantee}"
                for grantor, grantee in PENDING_GRANTS.keys() - found.keys()
            ),
            [],
            "remove resolved entries from PENDING_GRANTS",
        )

    def test_reference_rules(self) -> None:
        self.assertEqual(
            reference_violation("Oxygen.Editor.Shell", "Oxygen.Editor.Assets"),
            "shell does not reference domain",
        )
        self.assertEqual(
            reference_violation("Oxygen.Editor.Scenes", "Oxygen.Editor.LivePreview"),
            "a domain module references only domain modules below it",
        )
        self.assertEqual(
            reference_violation("Oxygen.Editor.Assets", "Oxygen.Editor.Runtime"),
            "only LivePreview references the engine",
        )
        self.assertEqual(
            reference_violation("Oxygen.Editor.Runtime", "Oxygen.Editor.Projects"),
            "foundation does not reference domain",
        )
        self.assertEqual(
            reference_violation("Oxygen.Editor.Shell", "Oxygen.Editor.App"),
            "nothing references the composition root",
        )
        self.assertIsNone(
            reference_violation("Oxygen.Editor.MaterialEditor", "Oxygen.Editor.Pickers")
        )
        self.assertIsNone(
            reference_violation("Oxygen.Editor.LivePreview", "Oxygen.Editor.Runtime")
        )
        self.assertIsNone(
            reference_violation("Oxygen.Editor.SceneEditor", "Oxygen.Editor.Runtime")
        )
        self.assertEqual(
            reference_violation("Oxygen.Editor.SceneEditor", "Oxygen.Editor.Interop"),
            "only LivePreview references the engine",
        )

    def test_grant_rules(self) -> None:
        module = PROJECTS / "Module"
        grantor = Project(module / "src" / "Module.csproj")
        builders = {
            "Module.Tests": Project(module / "tests" / "Module.Tests.csproj"),
            "Module.Probe": Project(
                module / "testsupport" / "Probe" / "Module.Probe.csproj"
            ),
            "Module.Demo": Project(module / "samples" / "Demo" / "Module.Demo.csproj"),
            "Other.Tests": Project(PROJECTS / "Other" / "tests" / "Other.Tests.csproj"),
            "Other": Project(PROJECTS / "Other" / "src" / "Other.csproj"),
            "Module": grantor,
        }
        own_tests = "internals are visible only to the module's own tests"
        self.assertIsNone(grant_violation(grantor, "Module.Tests", builders))
        self.assertIsNone(grant_violation(grantor, "Module.Probe", builders))
        self.assertIsNone(grant_violation(grantor, MOCK_PROXIES, builders))
        self.assertEqual(grant_violation(grantor, "Module.Demo", builders), own_tests)
        self.assertEqual(grant_violation(grantor, "Other.Tests", builders), own_tests)
        self.assertEqual(grant_violation(grantor, "Other", builders), own_tests)
        self.assertEqual(
            grant_violation(grantor, "Module", builders),
            "a project does not grant its internals to itself",
        )
        self.assertEqual(
            grant_violation(grantor, "Gone.Tests", builders),
            "no project builds that assembly",
        )


if __name__ == "__main__":
    unittest.main()
