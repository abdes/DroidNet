import subprocess
import logging
import os
from typing import List, Optional
import pathspec

logger = logging.getLogger("codemod.scanner")

class SymbolScanner:
    """Uses ripgrep to find files containing candidate symbols."""

    def __init__(self, root_dir: str, includes: List[str] = None, excludes: List[str] = None):
        self.root_dir = root_dir
        self.includes = includes or []
        self.excludes = excludes or []

    def scan(self, symbol: str) -> List[str]:
        """
        Runs ripgrep to find files containing the symbol.
        Respects .gitignore by default (ripgrep behavior).
        """
        # Build rg command
        # Options SHOULD come before the search path for robustness
        cmd = ["rg", "-l", "-w"]

        # Add exclusion globs if any
        for pattern in self.excludes:
            cmd.extend(["-g", f"!{pattern}"])

        # Add inclusion globs if any
        for pattern in self.includes:
            cmd.extend(["-g", pattern])

        # Add symbol and root directory last
        cmd.extend(["--", symbol, os.path.abspath(self.root_dir)])

        logger.debug("Running ripgrep: %s", " ".join(cmd))

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=False
            )

            files = []
            if result.returncode == 0:
                files = result.stdout.strip().splitlines()
            elif result.returncode == 1:
                return []
            else:
                logger.error("ripgrep failed with exit code %d: %s", result.returncode, result.stderr)
                return []

            # 2. Filter results using pathspec (REQ07)
            # We filter relative to the root_dir
            if not self.includes and not self.excludes:
                logger.info("Scanner found %d candidate files for '%s'", len(files), symbol)
                return files

            # Build exclusion spec
            exclude_spec = None
            if self.excludes:
                exclude_spec = pathspec.PathSpec.from_lines('gitwildmatch', self.excludes)

            # Build inclusion spec
            include_spec = None
            if self.includes:
                include_spec = pathspec.PathSpec.from_lines('gitwildmatch', self.includes)

            filtered_files = []
            abs_root = os.path.abspath(self.root_dir)

            for f in files:
                abs_f = os.path.abspath(f)
                rel_f = os.path.relpath(abs_f, abs_root)

                # If we have excludes, discard if matches
                if exclude_spec and exclude_spec.match_file(rel_f):
                    continue

                # If we have includes, MUST match one
                if include_spec:
                    if include_spec.match_file(rel_f):
                        filtered_files.append(f)
                else:
                    filtered_files.append(f)

            logger.info("Scanner found %d files (filtered from %d) for '%s'", len(filtered_files), len(files), symbol)
            return filtered_files

        except FileNotFoundError:
            logger.error("ripgrep (rg) not found in PATH. Please install it.")
            raise RuntimeError("ripgrep not found")
