"""Plan the full rename before emitting either patch."""

from pathlib import Path

from oxytools.common import ToolError
from oxytools.compilation import SOURCES

from ..drivers.cpp_driver import CppDriver
from ..drivers.hlsl_driver import HlslDriver
from ..drivers.json_driver import JsonDriver
from ..drivers.text_driver import TextDriver
from ..lexical import byte_span, spans
from ..patcher import PatchGenerator
from ..scanner import SymbolScanner


class RenameRefactoring:
    def run(self, context):
        args = context.args
        root = context.project_info.root_dir
        scanner = SymbolScanner(root, args.include, args.exclude)
        candidates = scanner.scan(args.from_sym)
        drivers = (CppDriver, HlslDriver, JsonDriver, TextDriver)
        supported = set().union(*(driver.SUPPORTED_EXTENSIONS for driver in drivers))
        candidates = [path for path in candidates if path.suffix.lower() in supported]
        if args.mode != "aggressive":
            candidates = [
                path
                for path in candidates
                if path.suffix.lower() not in TextDriver.SUPPORTED_EXTENSIONS
            ]
        for path in candidates:
            context.sources.read(path)
        safe, review = [], []
        for driver_class in drivers:
            files = [
                path
                for path in candidates
                if path.suffix.lower() in driver_class.SUPPORTED_EXTENSIONS
            ]
            if not files:
                continue
            if driver_class is CppDriver:
                old = args.from_sym.rsplit("::", 1)[-1]
                code_files = [
                    path
                    for path in files
                    if any(spans(context.sources.read(path).decode("utf-8"), old))
                ]
                if args.update_strings:
                    for path in files:
                        text = context.sources.read(path).decode("utf-8")
                        for start, end in spans(text, old, regions=True):
                            start, end = byte_span(text, start, end)
                            review.append(
                                context.sources.edit(
                                    path,
                                    start,
                                    end,
                                    args.to_sym,
                                    "C++ comment/string match; symbol identity is not established",
                                )
                            )
                if not code_files:
                    continue
                driver = driver_class(context)
                translation_files = {
                    path for path in scanner.files() if path.suffix.lower() in SOURCES
                }
                edits, proposed = driver.generate_edits(code_files, translation_files)
            else:
                edits, proposed = driver_class(context).generate_edits(files)
            safe.extend(edits)
            review.extend(proposed)
        if args.at and not safe:
            raise ToolError("--at did not select an editable C++ declaration")
        context.sources.verify()
        safe = sorted(set(safe), key=lambda edit: (edit.file_path, edit.offset))
        review = sorted(set(review), key=lambda edit: (edit.file_path, edit.offset))
        generator = PatchGenerator(root, context.sources)
        safe_patch = generator.render(safe)
        review_patch = generator.render(review, base_edits=safe)
        # Validate previews with the exact same replacement rules as patch mode.
        if not args.dry_run:
            outputs = []
            if safe_patch:
                outputs.append((Path(args.output_safe_patch), safe_patch))
            if review_patch:
                outputs.append((Path(args.output_review_patch), review_patch))
            generator.write(outputs)
        return safe, review
