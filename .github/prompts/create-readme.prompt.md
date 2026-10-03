---
mode: "agent"
description: "Create or update a concise, source-verified module README"
---

# Create or update a README

- Read the requested module's existing README, manifests, scripts and entrypoints,
  plus applicable `AGENTS.md`. Do not scan unrelated projects or fetch decorative
  examples by default. For Oxygen, follow `design/oxygen/RULES.md`.
- Update existing documentation in place. Explain purpose, public usage, relevant
  architecture and exact local setup/verification commands; include only sections
  the scope needs. Link shared setup, design and license authorities.
- Verify paths, commands and prerequisites against executable sources. Distinguish
  managed MSBuild/MTP from native Conan/CMake/CTest; do not invent CI or support
  claims. Mark unknowns rather than guessing.
- Use concise GFM and working relative links. Preserve useful existing content;
  avoid exhaustive trees, duplicated policies and cosmetic badges/logos.
- Validate changed files with `pre-commit run --files <paths>` and
  `git diff --check`; report content/link checks and any unrun verification.
