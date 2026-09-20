"""Rename safety, applicable patches, and real libclang semantics."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from codemod.cli import build_parser, main
from codemod.drivers.cpp_driver import load_clang
from codemod.drivers.hlsl_driver import HlslDriver
from codemod.drivers.json_driver import JsonDriver
from codemod.drivers.text_driver import TextDriver
from codemod.patcher import Edit, PatchGenerator, Sources, apply_edits
from codemod.project import ProjectResolver
from codemod.scanner import SymbolScanner
from oxytools.common import ToolError


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="codemod tests ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.git("init", "--quiet")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.safecrlf", "false")
        self.write(".clangd", "CompileFlags: {CompilationDatabase: .}\n")

    def git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.root, capture_output=True, check=True
        ).stdout

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode("utf-8") if isinstance(data, str) else data)
        return path

    def context(self, path, **kwargs):
        args = SimpleNamespace(
            from_sym="Old",
            to_sym="New",
            mode="aggressive",
            update_strings=False,
            **kwargs,
        )
        return SimpleNamespace(args=args, sources=Sources([path]))

    def invoke(self, *args):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            code = main(
                [
                    "rename",
                    "--root",
                    str(self.root),
                    "--from",
                    "Old",
                    "--to",
                    "New",
                    "--output-safe-patch",
                    str(self.root / "safe.patch"),
                    "--output-review-patch",
                    str(self.root / "review.patch"),
                    *args,
                ]
            )
        return code, output.getvalue()

    def database(self, files, extra=()):
        entries = [
            {
                "file": str(self.root / name),
                "directory": str(self.root),
                "output": f"Debug/{i}.o",
                "arguments": [
                    "clang++",
                    str(self.root / name),
                    "-std=c++20",
                    "-I",
                    str(self.root / "include"),
                    *extra,
                ],
            }
            for i, name in enumerate(files)
        ]
        self.write("compile_commands.json", json.dumps(entries))


class PatchTests(Fixture):
    def test_text_edits_preserve_surrounding_text_and_all_matches(self):
        path = self.write("notes.md", "Before Old, after Old; not Older.\n")
        ctx = self.context(path)
        safe, review = TextDriver(ctx).generate_edits([path])
        self.assertEqual(safe, [])
        self.assertEqual(len(review), 2)
        self.assertEqual(
            apply_edits(ctx.sources.read(path), review),
            b"Before New, after New; not Older.\n",
        )

    def test_unicode_bom_crlf_and_no_final_newline_patch_applies(self):
        path = self.write(
            "nested dir/café.cpp", "\ufeff// café\r\nint Old;\r\nint x = Old;"
        )
        sources = Sources([path])
        original = sources.read(path)
        offsets = [original.index(b"Old"), original.rindex(b"Old")]
        edits = [sources.edit(path, start, start + 3, "New") for start in offsets]
        generator = PatchGenerator(self.root, sources)
        payload = generator.render(edits)
        target = self.root / "safe.patch"
        generator.write([(target, payload)])
        self.git("apply", "--check", str(target))
        self.git("apply", str(target))
        self.assertEqual(path.read_bytes(), original.replace(b"Old", b"New"))
        self.assertIn(b"\\ No newline at end of file", payload)
        self.assertNotIn(str(self.root).encode(), payload)

    def test_stale_and_conflicting_edits_are_rejected(self):
        path = self.write("a.cpp", "Old Old\n")
        sources = Sources([path])
        edit = sources.edit(path, 0, 3, "New")
        with self.assertRaises(ToolError):
            apply_edits(sources.read(path), [edit, Edit(path, 0, "Old", "Other")])
        with self.assertRaises(ToolError):
            apply_edits(sources.read(path), [Edit(path, 4, "bad", "New")])
        generator = PatchGenerator(self.root, sources)
        payload = generator.render([edit])
        path.write_bytes(b"User changed this\n")
        with self.assertRaises(ToolError):
            generator.write([(self.root / "safe.patch", payload)])
        self.assertFalse((self.root / "safe.patch").exists())

    def test_existing_outputs_and_duplicate_destinations_are_rejected(self):
        path = self.write("a.cpp", "Old\n")
        generator = PatchGenerator(self.root, Sources([path]))
        output = self.write("safe.patch", "keep")
        with self.assertRaises(ToolError):
            generator.write([(output, b"replace")])
        self.assertEqual(output.read_text(), "keep")
        with self.assertRaises(ToolError):
            generator.write(
                [(self.root / "new.patch", b"a"), (self.root / "new.patch", b"b")]
            )
        self.assertFalse((self.root / "new.patch").exists())

    def test_review_patch_applies_after_safe_patch(self):
        path = self.write("a.cpp", "int Old; // Old\n")
        sources = Sources([path])
        safe = [sources.edit(path, 4, 7, "LongerName")]
        review = [sources.edit(path, 12, 15, "LongerName")]
        generator = PatchGenerator(self.root, sources)
        generator.write(
            [
                (self.root / "safe.patch", generator.render(safe)),
                (self.root / "review.patch", generator.render(review, base_edits=safe)),
            ]
        )
        self.git("apply", "safe.patch")
        self.git("apply", "--check", "review.patch")
        self.git("apply", "review.patch")
        self.assertEqual(path.read_text(), "int LongerName; // LongerName\n")

    def test_second_output_failure_removes_first_artifact(self):
        generator = PatchGenerator(self.root, Sources())
        from codemod import patcher

        original = patcher.atomic_write

        def fail_second(path, *args, **kwargs):
            if path.name == "review.patch":
                raise OSError("disk failure")
            return original(path, *args, **kwargs)

        with (
            patch("codemod.patcher.atomic_write", side_effect=fail_second),
            self.assertRaises(OSError),
        ):
            generator.write(
                [
                    (self.root / "safe.patch", b"safe"),
                    (self.root / "review.patch", b"review"),
                ]
            )
        self.assertFalse((self.root / "safe.patch").exists())
        self.assertFalse((self.root / "review.patch").exists())

    def test_patch_applies_from_an_edit_root_inside_a_git_repository(self):
        path = self.write("module/a.cpp", "int Old;\n")
        sources = Sources([path])
        generator = PatchGenerator(path.parent, sources)
        output = path.parent / "rename.patch"
        generator.write([(output, generator.render([sources.edit(path, 4, 7, "New")]))])
        subprocess.run(
            ["git", "apply", str(output)],
            cwd=path.parent,
            capture_output=True,
            check=True,
        )
        self.assertEqual(path.read_text(), "int New;\n")

    def test_cancelled_publication_cleans_reserved_artifacts(self):
        generator = PatchGenerator(self.root, Sources())
        with (
            patch("codemod.patcher.atomic_write", side_effect=KeyboardInterrupt),
            self.assertRaises(KeyboardInterrupt),
        ):
            generator.write(
                [
                    (self.root / "safe.patch", b"safe"),
                    (self.root / "review.patch", b"review"),
                ]
            )
        self.assertFalse((self.root / "safe.patch").exists())
        self.assertFalse((self.root / "review.patch").exists())


@unittest.skipUnless(
    importlib.util.find_spec("pathspec"), "Install the codemod optional dependencies"
)
class LexicalTests(Fixture):
    def test_hlsl_comments_strings_and_multiline_comments_are_protected(self):
        path = self.write(
            "a.hlsl",
            'float Old; // Old\n/* block\nOld */\nconst char* x = "Old";\nOld += 1;\n',
        )
        ctx = self.context(path)
        safe, review = HlslDriver(ctx).generate_edits([path])
        self.assertEqual(safe, [])
        self.assertEqual(len(review), 2)
        modified = apply_edits(ctx.sources.read(path), review).decode()
        self.assertIn('"Old"', modified)
        self.assertIn("Old */", modified)
        ctx.args.update_strings = True
        self.assertEqual(len(HlslDriver(ctx).generate_edits([path])[1]), 5)

    def test_json_replaces_every_complete_scalar_and_preserves_escapes(self):
        path = self.write(
            "a.json", '{"Old":"Old","unchanged":"Older","escaped":"O\\u006cd"}\n'
        )
        ctx = self.context(path)
        safe, review = JsonDriver(ctx).generate_edits([path])
        self.assertEqual(safe, [])
        self.assertEqual(len(review), 3)
        self.assertEqual(
            json.loads(apply_edits(ctx.sources.read(path), review)),
            {"New": "New", "unchanged": "Older", "escaped": "New"},
        )

    def test_duplicate_json_key_after_rename_is_rejected(self):
        path = self.write("a.json", '{"Old":1,"New":2}\n')
        with self.assertRaises(ToolError):
            JsonDriver(self.context(path)).generate_edits([path])

    def test_yaml_scalars_preserve_comments_and_types(self):
        path = self.write("a.yaml", "# Old\nOld: 'Old'\nvalue: Old\n")
        ctx = self.context(path)
        ctx.args.to_sym = "null"
        safe, review = JsonDriver(ctx).generate_edits([path])
        self.assertEqual(safe, [])
        self.assertEqual(len(review), 3)
        result = apply_edits(ctx.sources.read(path), review).decode()
        import yaml

        self.assertEqual(yaml.safe_load(result), {"null": "null", "value": "null"})
        self.assertTrue(result.startswith("# Old\n"))

    def test_discovery_is_literal_and_cannot_override_gitignore(self):
        self.write(".gitignore", "ignored.txt\n")
        self.write("ignored.txt", "a.b\n")
        literal = self.write("literal.txt", "a.b\n")
        self.write("other.txt", "axb\n")
        scanner = SymbolScanner(self.root, ["*.txt"])
        self.assertEqual(scanner.scan("a.b"), [literal])

    def test_scanner_failure_is_not_an_empty_success(self):
        error = subprocess.CompletedProcess([], 2, b"", b"read error")
        with (
            patch("codemod.scanner.subprocess.run", return_value=error),
            self.assertRaisesRegex(ToolError, "read error"),
        ):
            SymbolScanner(self.root).scan("Old")

    def test_explicit_root_is_not_widened_to_parent_configuration(self):
        child = self.root / "child"
        child.mkdir()
        result = ProjectResolver.resolve(child, explicit=True)
        self.assertEqual(result.root_dir, child)
        self.assertEqual(result.configuration_root, self.root)

    def test_global_options_before_and_after_subcommand(self):
        for arguments in (
            ["--root", "before", "rename", "--from", "Old", "--to", "New"],
            ["rename", "--from", "Old", "--to", "New", "--root", "before"],
        ):
            self.assertEqual(build_parser().parse_args(arguments).root, "before")

    def test_dry_run_is_visible_and_never_writes_outputs(self):
        source = self.write("a.md", "Old is documented.\n")
        code, output = self.invoke("--mode", "aggressive", "--dry-run")
        self.assertEqual(code, 0, output)
        self.assertIn("review:", output)
        self.assertIn("'Old' -> 'New'", output)
        self.assertFalse((self.root / "review.patch").exists())
        self.assertEqual(source.read_text(), "Old is documented.\n")

    def test_data_only_rename_does_not_load_libclang(self):
        self.write("a.json", '{"Old":"Old"}\n')
        with patch(
            "codemod.drivers.cpp_driver.load_clang", side_effect=AssertionError("clang")
        ):
            code, output = self.invoke()
        self.assertEqual(code, 0, output)
        self.assertFalse((self.root / "safe.patch").exists())
        self.assertTrue((self.root / "review.patch").exists())

    def test_qualified_text_proposals_preserve_namespace_prefix(self):
        cases = (
            (TextDriver, "a.md", "Use N::Old here.\n"),
            (HlslDriver, "a.hlsl", "N::Old();\n"),
            (JsonDriver, "a.json", '{"value":"N::Old"}\n'),
        )
        for driver, name, text in cases:
            with self.subTest(name=name):
                path = self.write(name, text)
                context = self.context(path)
                context.args.from_sym = "N::Old"
                safe, review = driver(context).generate_edits([path])
                self.assertEqual(safe, [])
                self.assertIn(
                    b"N::New", apply_edits(context.sources.read(path), review)
                )

    def test_invalid_data_prevents_all_patch_outputs(self):
        shader = self.write("a.hlsl", "float Old;\n")
        self.write("a.json", '{"Old": broken}')
        code, output = self.invoke()
        self.assertEqual(code, 2, output)
        self.assertEqual(shader.read_text(), "float Old;\n")
        self.assertFalse((self.root / "review.patch").exists())

    def test_qualified_noop_is_rejected_without_claiming_a_patch(self):
        with (
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit) as error,
        ):
            main(["rename", "--from", "N::Old", "--to", "Old"])
        self.assertEqual(error.exception.code, 2)


class CppTests(Fixture):
    @classmethod
    def setUpClass(cls):
        if importlib.util.find_spec("pathspec") is None:
            raise unittest.SkipTest("Install the codemod optional dependencies")
        try:
            load_clang()
        except (ModuleNotFoundError, ToolError) as error:
            raise unittest.SkipTest(str(error)) from error

    def test_recorded_flags_and_source_not_last_are_respected(self):
        path = self.write(
            "src/a.cpp",
            "#if FEATURE\nint Old(){return 1;}\nint f(){return Old();}\n#endif\n",
        )
        self.database(["src/a.cpp"], ["-DFEATURE=1"])
        code, output = self.invoke("--kind", "function")
        self.assertEqual(code, 0, output)
        self.git("apply", "--check", "safe.patch")
        self.git("apply", "safe.patch")
        self.assertIn("int New()", path.read_text())
        self.assertIn("return New();", path.read_text())

    def test_qualified_overload_requires_location_and_changes_one_overload(self):
        path = self.write(
            "a.cpp",
            "namespace N {\nint Old(int x){return x;}\ndouble Old(double x){return x;}\n}\nint f(){return N::Old(1);}\n",
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--from", "N::Old", "--kind", "function")
        self.assertEqual(code, 2, output)
        self.assertIn("Ambiguous", output)
        self.assertFalse((self.root / "safe.patch").exists())
        code, output = self.invoke(
            "--from", "N::Old", "--kind", "function", "--at", "a.cpp:2:5"
        )
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertIn("int New(int x)", path.read_text())
        self.assertIn("double Old(double x)", path.read_text())
        self.assertIn("N::New(1)", path.read_text())

    def test_shadowed_variables_require_selection(self):
        path = self.write(
            "a.cpp",
            "int Old=1;\nint f(){int Old=2; return Old;}\nint g(){return Old;}\n",
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--kind", "variable")
        self.assertEqual(code, 2, output)
        code, output = self.invoke("--kind", "variable", "--at", "a.cpp:1:5")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertIn("int New=1", path.read_text())
        self.assertIn("int Old=2; return Old", path.read_text())
        self.assertIn("return New;", path.read_text())

    def test_class_constructor_destructor_and_references(self):
        path = self.write(
            "a.cpp",
            "struct Old { Old(); ~Old(); };\nOld::Old() {}\nOld::~Old() {}\nint f(){ Old value; return 0; }\n",
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--kind", "class")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertNotIn("Old", path.read_text())

    def test_template_class_and_instantiation(self):
        path = self.write(
            "a.cpp", "template<class T> struct Old { Old() {} };\nOld<int> value;\n"
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--kind", "class")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertNotIn("Old", path.read_text())

    def test_virtual_method_renames_override_chain(self):
        path = self.write(
            "a.cpp",
            "struct Base { virtual int Old(){return 1;} };\nstruct Derived: Base { int Old() override {return 2;} };\nint f(Base& b){return b.Old();}\n",
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--from", "Base::Old", "--kind", "function")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertNotIn("Old", path.read_text())

    def test_headers_are_analyzed_through_consumers_without_literal_matches(self):
        path = self.write("include/api.h", "struct Old {};\n")
        self.write("a.cpp", '#include "api.h"\nint x;\n')
        self.database(["a.cpp"])
        code, output = self.invoke("--include", "include/*.h", "--kind", "class")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertEqual(path.read_text(), "struct New {};\n")

    def test_parse_errors_do_not_emit_partial_patches(self):
        path = self.write("a.cpp", '#include "missing.h"\nint Old;\n')
        self.database(["a.cpp"])
        code, output = self.invoke()
        self.assertEqual(code, 2, output)
        self.assertIn("parsing failed", output)
        self.assertFalse((self.root / "safe.patch").exists())
        self.assertIn("Old", path.read_text())

    def test_name_collision_is_rejected_even_when_overloading_would_compile(self):
        self.write(
            "a.cpp",
            "int Old(int x){return x;}\nint New(double x){return 0;}\nint f(){return New(1);}\n",
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--kind", "function")
        self.assertEqual(code, 2, output)
        self.assertIn("already exists", output)
        self.assertFalse((self.root / "safe.patch").exists())

    def test_unicode_before_symbol_uses_byte_offsets(self):
        path = self.write(
            "a.cpp", '// café\nconst char* s="é"; int Old=1;\nint f(){return Old;}\n'
        )
        self.database(["a.cpp"])
        code, output = self.invoke("--kind", "variable")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertIn('s="é"; int New=1', path.read_text())

    def test_no_parse_fallback_when_command_is_invalid(self):
        self.write("a.cpp", "int Old;\n")
        self.database(["a.cpp"], ["-not-a-real-compiler-option"])
        code, output = self.invoke()
        self.assertEqual(code, 2, output)
        self.assertFalse((self.root / "safe.patch").exists())

    def test_cross_file_references_are_found_for_qualified_names(self):
        header = self.write("include/api.h", "namespace N { int Old(); }\n")
        implementation = self.write(
            "impl.cpp", '#include "api.h"\nint N::Old(){return 1;}\n'
        )
        caller = self.write(
            "use.cpp", '#include "api.h"\nusing N::Old;\nint f(){return Old();}\n'
        )
        self.database(["impl.cpp", "use.cpp"])
        code, output = self.invoke("--from", "N::Old", "--kind", "function")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        for path in (header, implementation, caller):
            self.assertNotIn("Old", path.read_text())

    def test_strings_in_other_cpp_files_are_reviewed_only_when_requested(self):
        self.write("a.cpp", "int Old; // Old\n")
        text_only = self.write("comment.cpp", "// Old is documented here\n")
        self.database(["a.cpp", "comment.cpp"])
        code, output = self.invoke("--update-strings")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertIn("Old", text_only.read_text())
        self.git("apply", "review.patch")
        self.assertNotIn("Old", text_only.read_text())

    def test_inactive_branch_is_reviewed_instead_of_silently_omitted(self):
        self.write("a.cpp", "int Old;\n#if 0\nint x=Old;\n#endif\n")
        self.database(["a.cpp"])
        code, output = self.invoke()
        self.assertEqual(code, 0, output)
        review = (self.root / "review.patch").read_text()
        self.assertIn("+int x=New;", review)

    def test_macro_reference_that_would_break_after_rename_blocks_patches(self):
        self.write("a.cpp", "#define USE Old\nint Old;\nint f(){return USE;}\n")
        self.database(["a.cpp"])
        code, output = self.invoke()
        self.assertEqual(code, 2, output)
        self.assertFalse((self.root / "safe.patch").exists())

    def test_configuration_filter_skips_unbuilt_release(self):
        self.write("a.cpp", "int Old;\n")
        self.database(["a.cpp"])
        entries = json.loads((self.root / "compile_commands.json").read_text())
        entries.append(
            {
                **entries[0],
                "output": "Release/a.o",
                "arguments": [
                    "clang++",
                    "@missing-release.rsp",
                    str(self.root / "a.cpp"),
                ],
            }
        )
        self.write("compile_commands.json", json.dumps(entries))
        code, output = self.invoke()
        self.assertEqual(code, 0, output)

    def test_raw_strings_and_digit_separators_do_not_confuse_token_discovery(self):
        path = self.write("a.cpp", 'int Old=1\'000; const char* s=R"tag(Old)tag";\n')
        self.database(["a.cpp"])
        code, output = self.invoke()
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertIn("int New=1'000", path.read_text())
        self.assertIn('R"tag(Old)tag"', path.read_text())

    def test_contexts_with_different_symbol_bindings_reject_shared_edit(self):
        self.write(
            "a.cpp",
            "struct A { int Old; };\nstruct B { int Old; };\n#ifdef SELECT_A\nusing Choice=A;\n#else\nusing Choice=B;\n#endif\nint f(Choice& x){return x.Old;}\n",
        )
        self.database(["a.cpp"], ["-DSELECT_A"])
        entries = json.loads((self.root / "compile_commands.json").read_text())
        entries.append(
            {
                **entries[0],
                "output": "Release/a.o",
                "arguments": entries[0]["arguments"][:-1],
            }
        )
        self.write("compile_commands.json", json.dumps(entries))
        code, output = self.invoke(
            "--configuration", "all", "--from", "A::Old", "--kind", "member"
        )
        self.assertEqual(code, 2, output)
        self.assertIn("disagree", output)
        self.assertFalse((self.root / "safe.patch").exists())

    def test_same_basename_static_symbols_are_not_merged(self):
        first = self.write(
            "first/same.cpp", "static int Old;\nint first(){return Old;}\n"
        )
        second = self.write(
            "second/same.cpp", "static int Old;\nint second(){return Old;}\n"
        )
        self.database(["first/same.cpp", "second/same.cpp"])
        code, output = self.invoke("--kind", "variable")
        self.assertEqual(code, 2, output)
        self.assertIn("Ambiguous", output)
        code, output = self.invoke("--kind", "variable", "--at", "first/same.cpp:1:12")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertNotIn("Old", first.read_text())
        self.assertIn("Old", second.read_text())

    def test_unrelated_static_new_name_in_another_unit_is_not_a_collision(self):
        first = self.write("a.cpp", "static int Old;\nint first(){return Old;}\n")
        self.write("b.cpp", "static int New;\nint second(){return New;}\n")
        self.database(["a.cpp", "b.cpp"])
        code, output = self.invoke("--kind", "variable")
        self.assertEqual(code, 0, output)
        self.git("apply", "safe.patch")
        self.assertNotIn("Old", first.read_text())


if __name__ == "__main__":
    unittest.main()
