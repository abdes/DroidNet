import logging
import os
from typing import List, Tuple, Optional
import clang.cindex

from ..patcher import Edit

logger = logging.getLogger("codemod.drivers.cpp")


class CppDriver:
    """AST-aware C++ refactoring driver using libclang."""

    SUPPORTED_EXTENSIONS = {".h", ".hpp", ".hh", ".cpp", ".cxx", ".cc"}

    def __init__(self, ctx):
        self.ctx = ctx
        self.index = None
        self.compdb = None

        # Try to find libclang if not in PATH
        common_paths = [
            r"C:\Program Files\LLVM\bin",
            r"C:\Program Files\LLVM\lib",
            r"D:\Program Files\LLVM\bin",
            r"F:\Program Files\LLVM\bin",  # Added based on log analysis
        ]

        # If it fails to init normally, try loading from common paths
        try:
            self.index = clang.cindex.Index.create()
        except Exception:
            for path in common_paths:
                lib_path = os.path.join(path, "libclang.dll")
                if os.path.exists(lib_path):
                    logger.info(
                        "Found libclang at %s, setting library path.", path
                    )
                    clang.cindex.Config.set_library_path(path)
                    try:
                        self.index = clang.cindex.Index.create()
                        break
                    except Exception as e:
                        logger.error(
                            "Failed to init clang with path %s: %s", path, e
                        )

        if not self.index:
            logger.error(
                "Failed to initialize libclang index. C++ renames will be skipped."
            )
            return

        if self.ctx.project_info.compilation_database_dir:
            try:
                self.compdb = clang.cindex.CompilationDatabase.fromDirectory(
                    self.ctx.project_info.compilation_database_dir
                )
                logger.info(
                    "Loaded compilation database from %s",
                    self.ctx.project_info.compilation_database_dir,
                )
            except Exception as e:
                logger.error("Failed to load compilation database: %s", e)

    def generate_edits(
        self, files: List[str], progress_callback=None
    ) -> Tuple[List[Edit], List[Edit]]:
        """Return (safe_edits, review_edits)."""
        if not self.index:
            return [], []

        safe_edits = []
        review_edits = []

        cpp_files = [
            f
            for f in files
            if os.path.splitext(f)[1].lower() in self.SUPPORTED_EXTENSIONS
        ]

        for file_path in cpp_files:
            # Notify progress start
            if progress_callback:
                try:
                    progress_callback(file_path, True, None, None)
                except Exception:
                    pass
            logger.info("Processing C++ file: %s", file_path)
            # per-file counts (avoid scanning whole lists later)
            file_safe_count = 0
            file_unsafe_count = 0
            try:
                # Basic translation unit parsing
                args = ["-std=c++23"]
                if self.compdb:
                    cmds = self.compdb.getCompileCommands(file_path)

                    # Heuristic for headers (REQ13 fallback)
                    if not cmds:
                        ext = os.path.splitext(file_path)[1].lower()
                        if ext in {".h", ".hpp", ".hh"}:
                            for cpp_ext in [".cpp", ".cc", ".cxx"]:
                                alt_path = (
                                    os.path.splitext(file_path)[0] + cpp_ext
                                )
                                cmds = self.compdb.getCompileCommands(alt_path)
                                if cmds:
                                    logger.debug(
                                        "Using fallback commands from sibling %s for header %s",
                                        alt_path,
                                        file_path,
                                    )
                                    break

                    if not cmds:
                        # Try same directory
                        pass

                    if cmds and len(cmds) > 0:
                        cmd_args = list(cmds[0].arguments)
                        if len(cmd_args) > 2:
                            args = cmd_args[1:-1]
                            logger.debug(
                                "Using compile commands for %s: %s",
                                file_path,
                                args,
                            )
                        else:
                            logger.warning(
                                "Compile command for %s too short: %s",
                                file_path,
                                cmd_args,
                            )
                    else:
                        logger.warning(
                            "No compile command found for %s in database",
                            file_path,
                        )

                filtered_args = self._filter_flags(args)
                # Strip trailing '--' because index.parse appends the filename,
                # and having '--' at the end might be redundant or problematic if libclang bindings add it too?
                # Actually, index.parse handles it, but let's be safe and ensure we don't double up or confuse it.
                if filtered_args and filtered_args[-1] == "--":
                    filtered_args.pop()

                try:
                    tu = self.index.parse(file_path, args=filtered_args)
                except clang.cindex.TranslationUnitLoadError:
                    logger.error("Fatal libclang error parsing %s", file_path)
                    logger.error("Failed command args: %s", filtered_args)

                    # Retry without flags to see if file is fundamentally broken
                    try:
                        fallback_args = ["-std=c++23"]
                        tu = self.index.parse(file_path, args=fallback_args)
                        logger.info(
                            "Fallback parse (no flags) SUCCEEDED. The issue is likely in the compiler flags."
                        )
                        # Proceed with fallback TU? No, it often yields bad semantic results.
                        # But we can continue to avoid crashing.
                    except Exception as e2:
                        logger.error("Fallback parse also failed: %s", e2)
                        continue  # Skip this file

                # Check diagnostics
                for diag in tu.diagnostics:
                    logger.warning("Clang Diagnostic: %s", diag)

                # Read file lines once (avoid repeated opens inside AST visitor)
                try:
                    with open(file_path, "r", encoding="utf-8") as _f:
                        file_lines = _f.readlines()
                except Exception:
                    file_lines = None

                # Traverse AST (pass in file lines to avoid repeated file opens)
                edits, review = self._find_symbol_references(
                    tu.cursor, file_path, file_lines
                )
                safe_edits.extend(edits)
                review_edits.extend(review)
                # Track per-file counts for progress reporting (len of results)
                file_safe_count = len(edits)
                file_unsafe_count = len(review)
            except Exception as e:
                logger.error("Unexpected error parsing %s: %s", file_path, e)
            finally:
                # Notify progress end for this file: compute counts for this file only
                if progress_callback:
                    try:
                        # Use per-file counts if available
                        progress_callback(
                            file_path, False, file_safe_count, file_unsafe_count
                        )
                    except Exception:
                        pass

        return safe_edits, review_edits

    def _filter_flags(self, args: List[str]) -> List[str]:
        """Apply .clangd Add/Remove rules (REQ13)."""
        if (
            not self.ctx.project_info.add_flags
            and not self.ctx.project_info.remove_flags
        ):
            return args

        import fnmatch

        filtered = []

        # 1. Remove rules
        for arg in args:
            keep = True
            for rule in self.ctx.project_info.remove_flags:
                if rule.endswith("*"):
                    pattern = rule[:-1]
                    if arg.startswith(pattern):
                        keep = False
                        break
                elif arg == rule:
                    keep = False
                    break
            if keep:
                filtered.append(arg)

        # 2. Add rules (REQ13: Insert before '--' if present)
        if self.ctx.project_info.add_flags:
            try:
                # Find the separator that denotes end of flags in clang/cl
                separator_idx = filtered.index("--")
                # Insert BEFORE the separator
                for flag in reversed(self.ctx.project_info.add_flags):
                    filtered.insert(separator_idx, flag)
            except ValueError:
                # No separator, append to end
                filtered.extend(self.ctx.project_info.add_flags)

        logger.debug("Filtered flags: %s", filtered)
        return filtered

    def _get_qualified_name(self, cursor) -> str:
        """Heuristic to get qualified name: A::B::C."""
        parts = []
        curr = cursor
        while curr and curr.kind not in {
            clang.cindex.CursorKind.TRANSLATION_UNIT,
            clang.cindex.CursorKind.INVALID_FILE,
        }:
            if curr.spelling:
                parts.append(curr.spelling)
            curr = curr.semantic_parent
        return "::".join(reversed(parts))

    def _find_symbol_references(
        self, cursor, target_file: str, file_lines: Optional[List[str]] = None
    ) -> Tuple[List[Edit], List[Edit]]:
        """Recursively find cursors matching the from_symbol semantically."""
        safe_edits = []
        review_edits = []
        from_symbol = self.ctx.args.from_sym
        target_name = from_symbol.split("::")[-1]
        abs_target_file = os.path.abspath(target_file)

        # 1. Identify "Target USRs".
        # We search the whole TU for declarations that match our from_symbol string.
        target_usrs = set()

        def find_targets(node):
            if self._matches_kind(node):
                qname = self._get_qualified_name(node)

                # If identifier contains '::', match exactly (precise)
                if "::" in from_symbol:
                    if qname == from_symbol:
                        target_usrs.add(node.get_usr())
                else:
                    # If just a name, match any declaration with that name (broader)
                    if node.spelling == from_symbol:
                        target_usrs.add(node.get_usr())

            for child in node.get_children():
                find_targets(child)

        find_targets(cursor)
        if not target_usrs:
            logger.debug(
                "No target USRs found for %s in this translation unit.",
                from_symbol,
            )

        # 2. Traverse and match occurrences
        # Use a set to avoid duplicate edits at the same location
        seen_locs = set()

        def visitor(node, parent):
            if (
                node.location.file
                and os.path.abspath(node.location.file.name) == abs_target_file
            ):
                node_name = node.spelling
                if node_name == target_name:
                    # HEURISTIC: Check if the spelling actually starts at node.location.
                    # This filters out CALL_EXPR wrappers that point to the start of the prefix.
                    loc = node.location
                    try:
                        lines = file_lines
                        if lines is not None and loc.line <= len(lines):
                            line_text = lines[loc.line - 1]
                            actual_token = line_text[
                                loc.column
                                - 1 : loc.column
                                - 1
                                + len(target_name)
                            ]
                            if actual_token != target_name:
                                # This node points somewhere else (wrapper)
                                for child in node.get_children():
                                    visitor(child, node)
                                return
                    except Exception:
                        pass

                    loc_key = (loc.line, loc.column)
                    if loc_key not in seen_locs:
                        match = False
                        ref = node.referenced

                        # Use ref USR if available, else node USR
                        effective_usr = ref.get_usr() if ref else node.get_usr()

                        if effective_usr in target_usrs:
                            match = True
                            logger.debug(
                                "Safe match: %s at %d:%d (USR: %s)",
                                node_name,
                                loc.line,
                                loc.column,
                                effective_usr,
                            )
                        else:
                            logger.debug(
                                "Ambiguous match: %s at %d:%d (USR: %s, Targets: %s)",
                                node_name,
                                loc.line,
                                loc.column,
                                effective_usr,
                                list(target_usrs),
                            )

                        if match:
                            safe_edits.append(
                                self._create_edit(
                                    node, target_file, target_name
                                )
                            )
                            seen_locs.add(loc_key)
                        elif node.kind not in {
                            clang.cindex.CursorKind.PARM_DECL
                        }:
                            review_edits.append(
                                self._create_edit(
                                    node, target_file, target_name
                                )
                            )
                            seen_locs.add(loc_key)

            for child in node.get_children():
                visitor(child, node)

        visitor(cursor, None)
        return safe_edits, review_edits

    def _create_edit(self, node, target_file, target_name) -> Edit:
        loc = node.location
        return Edit(
            file_path=target_file,
            original_text=target_name,
            replacement_text=self.ctx.args.to_sym.split("::")[-1],
            start_line=loc.line - 1,
            end_line=loc.line - 1,
            start_col=loc.column - 1,
            end_col=loc.column - 1 + len(target_name),
        )

    def _matches_kind(self, cursor) -> bool:
        k = self.ctx.args.kind
        ck = cursor.kind

        # If no kind specified, or if it's a reference to something we care about
        if not k:
            return True

        # Common kinds for renames
        if k == "class":
            return ck in {
                clang.cindex.CursorKind.CLASS_DECL,
                clang.cindex.CursorKind.STRUCT_DECL,
                clang.cindex.CursorKind.CLASS_TEMPLATE,
                clang.cindex.CursorKind.TYPE_REF,
                clang.cindex.CursorKind.TEMPLATE_REF,
            }
        if k == "function":
            return ck in {
                clang.cindex.CursorKind.FUNCTION_DECL,
                clang.cindex.CursorKind.CXX_METHOD,
                clang.cindex.CursorKind.CALL_EXPR,
                clang.cindex.CursorKind.MEMBER_REF_EXPR,
                clang.cindex.CursorKind.DECL_REF_EXPR,
            }
        if k == "variable":
            return ck in {
                clang.cindex.CursorKind.VAR_DECL,
                clang.cindex.CursorKind.PARM_DECL,
                clang.cindex.CursorKind.FIELD_DECL,
                clang.cindex.CursorKind.DECL_REF_EXPR,
                clang.cindex.CursorKind.MEMBER_REF_EXPR,
            }
        if k == "member":
            return ck in {
                clang.cindex.CursorKind.FIELD_DECL,
                clang.cindex.CursorKind.CXX_METHOD,
                clang.cindex.CursorKind.MEMBER_REF_EXPR,
            }

        return (
            ck == clang.cindex.CursorKind.NAMESPACE
            if k == "namespace"
            else True
        )
