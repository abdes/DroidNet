"""Scope selection semantics and bounded filesystem work for large scopes."""

from __future__ import annotations

import os
import re
from pathlib import Path
from unittest.mock import patch

from test_oxytidy import Fixture


class IndexedScopeTests(Fixture):
    def test_mixed_roots_preserve_exact_sources_and_header_discovery(self):
        exact = self.write("src/exact/a.cpp", "")
        sibling = self.write("src/exact/b.cpp", "")
        header = self.write("src/include/a.h", "")
        consumer = self.write("src/include/sub/consumer.cpp", "")
        nested = self.write("src/module/sub/a.cpp", "")
        lookalike = self.write("src/module-extra/a.cpp", "")
        scope = self.scope(str(exact), str(header), "src/module")
        for path, selected, discovered in (
            (exact, True, True),
            (sibling, False, False),
            (header, True, True),
            (consumer, False, True),
            (nested, True, True),
            (lookalike, False, False),
        ):
            with self.subTest(path=path):
                self.assertEqual(scope.contains(path), selected)
                self.assertEqual(scope.discovery_contains(path), discovered)
        self.assertEqual(scope.inputs(), sorted([exact, header, nested]))

    def test_overlapping_roots_are_traversed_once(self):
        source = self.write("src/module/sub/a.cpp", "")
        scope = self.scope("src/module", "src/module/sub", str(source), str(source))
        visited = []
        original = Path.rglob

        def record(path, *args, **kwargs):
            visited.append(path)
            return original(path, *args, **kwargs)

        with patch.object(Path, "rglob", record):
            self.assertEqual(scope.inputs(), [source])
        self.assertEqual(visited, [self.root / "src/module"])

    def test_lookup_filesystem_work_does_not_grow_with_scope_size(self):
        files = [self.write(f"src/file-{index}.cpp", "") for index in range(128)]
        outside = self.write("src/unselected.cpp", "")
        original = Path.resolve
        counts = []
        for roots in ([files[-1]], files):
            scope = self.scope(*(str(path) for path in roots))
            # Construct the per-invocation indexes before measuring lookups.
            scope.contains(files[-1])
            scope.discovery_contains(files[-1])
            calls = []

            def record(path, *args, **kwargs):
                calls.append(path)
                return original(path, *args, **kwargs)

            with (
                patch.object(Path, "resolve", record),
                patch.object(Path, "is_dir", side_effect=AssertionError("root stat")),
            ):
                for contains in (scope.contains, scope.discovery_contains):
                    self.assertTrue(contains(files[-1]))
                    self.assertFalse(contains(outside))
            counts.append(len(calls))
        self.assertEqual(counts[0], counts[1])
        self.assertLess(counts[1], 20)

    def test_directory_selection_still_excludes_vendor_and_tests(self):
        normal = self.write("src/module/a.cpp", "")
        vendor = self.write("src/vendor/a.cpp", "")
        test = self.write("src/module/Test/a.cpp", "")
        scope = self.scope("src")
        for contains in (scope.contains, scope.discovery_contains):
            self.assertTrue(contains(normal))
            self.assertFalse(contains(vendor))
            self.assertFalse(contains(test))
        self.assertTrue(self.scope("src", tests=True).discovery_contains(test))

    def test_query_symlinks_are_resolved_fresh(self):
        source = self.write("src/module/a.cpp", "")
        outside = self.write("external/a.cpp", "")
        alias = self.root / "src/module/link.cpp"
        try:
            alias.symlink_to(source)
        except OSError as error:
            self.skipTest(f"File symlinks unavailable: {error}")
        scope = self.scope("src/module")
        self.assertTrue(scope.contains(alias))
        self.assertTrue(scope.discovery_contains(alias))
        alias.unlink()
        alias.symlink_to(outside)
        self.assertFalse(scope.contains(alias))
        self.assertFalse(scope.discovery_contains(alias))
        self.assertEqual(scope.inputs(), [source])

    def test_dependency_filter_retains_compiler_spelling_and_scope(self):
        source = self.write("src/module/a.h", "")
        sibling = self.write("src/module/b.h", "")
        scope = self.scope(str(source))
        names = ["src/module/../module/a.h", str(sibling)]
        expression = scope.dependency_filter(names, self.root)
        self.assertIsNotNone(re.fullmatch(expression, names[0]))
        self.assertIsNotNone(re.fullmatch(expression, str(source)))
        self.assertIsNone(re.fullmatch(expression, str(sibling)))
        if os.name == "nt":
            self.assertTrue(scope.contains(Path(str(source).swapcase())))
