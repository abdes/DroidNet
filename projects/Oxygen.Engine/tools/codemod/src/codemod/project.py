import os
import yaml
import logging
from dataclasses import dataclass, field
from typing import Optional, List

logger = logging.getLogger("codemod.project")

@dataclass
class ProjectInfo:
    root_dir: str
    compilation_database_dir: Optional[str] = None
    add_flags: List[str] = field(default_factory=list)
    remove_flags: List[str] = field(default_factory=list)

class ProjectResolver:
    """Resolves project root and configuration like .clangd."""

    @staticmethod
    def resolve(start_path: str) -> ProjectInfo:
        """
        Heuristically find the project root and parse .clangd if present.
        start_path: Where to begin looking (usually os.getcwd())
        """
        curr = os.path.abspath(start_path)
        potential_roots = []

        # 1. Search upwards for project markers
        markers = {".git", ".clangd", "conanfile.py", "pyproject.toml", "CMakeLists.txt"}
        while curr:
            if any(os.path.exists(os.path.join(curr, m)) for m in markers):
                potential_roots.append(curr)
            parent = os.path.dirname(curr)
            if parent == curr:
                break
            curr = parent

        if not potential_roots:
            return ProjectInfo(root_dir=os.path.abspath(start_path))

        # 2. Prioritize root with .clangd
        root = potential_roots[0] # Default to closest
        for pr in potential_roots:
            if os.path.exists(os.path.join(pr, ".clangd")):
                root = pr
                break

        info = ProjectInfo(root_dir=root)

        # 2. Parse .clangd if it exists in root
        clangd_path = os.path.join(root, ".clangd")
        if os.path.exists(clangd_path):
            try:
                with open(clangd_path, 'r', encoding='utf-8') as f:
                    data = yaml.safe_load(f)
                    if data and "CompileFlags" in data:
                        flags = data["CompileFlags"]
                        comp_db = flags.get("CompilationDatabase")
                        if comp_db:
                            # CompilationDatabase is usually relative to the .clangd file
                            if not os.path.isabs(comp_db):
                                comp_db = os.path.abspath(os.path.join(root, comp_db))

                            if os.path.exists(comp_db):
                                info.compilation_database_dir = comp_db
                                logger.info("Detected compilation database via .clangd: %s", comp_db)
                            else:
                                logger.warning("Compilation database path in .clangd does not exist: %s", comp_db)

                        info.add_flags = flags.get("Add", [])
                        if isinstance(info.add_flags, str):
                            info.add_flags = [info.add_flags]

                        info.remove_flags = flags.get("Remove", [])
                        if isinstance(info.remove_flags, str):
                            info.remove_flags = [info.remove_flags]
            except Exception as e:
                logger.error("Failed to parse .clangd at %s: %s", clangd_path, e)

        return info
