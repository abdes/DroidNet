---
name: docs_agent
description: Maintain concise, source-verified DroidNet and Oxygen documentation
---

# Documentation scope

- Follow root/module `AGENTS.md`; Oxygen work also follows
  `design/oxygen/RULES.md`. Sources live in `projects/<module>`, not a root React
  app. Verify C#/WinUI and native Conan/CMake workflows separately.
- Read the owning manifests, scripts and representative entrypoints before making
  claims. Current build commands live in `tooling/doc/build.md` and the engine's
  build/preset guides; do not copy stale README examples unchecked.
- Windows command examples must select 64-bit MSBuild (`Bin\amd64`) and
  x64-hosted compilers, never 32-bit MSBuild or `Hostx86`. Retain
  `/p:PreferredToolArchitecture=x64` and the same VS installation; see
  [Windows tool policy](../../tooling/doc/build.md#windows-tool-policy).
- Update the requested document in place. Place new documentation beside its
  owning module or under the existing `design/`, `plan/` or `tooling/doc/`
  hierarchy; do not invent a root `docs/` tree or duplicate facts by default.
- Folder READMEs explain local purpose and usage; project READMEs add only the
  architecture/setup/workflow needed there. Link shared instructions instead of
  repeating toolchain versions, licensing or repository-wide setup.
- Specifications own contracts; plans own sequencing; validation summaries own
  evidence. Preserve accepted decisions, stable IDs and unresolved work when
  consolidating. Obtain approval before changing an accepted contract.
- Keep prose factual and compact. Do not invent CI, coverage, owners, completion
  or validation. Do not modify implementation/configuration or run unrelated
  builds as part of a documentation-only request.
- From the root, validate changed files with `pre-commit run --files <paths>` and
  `git diff --check`. Check links/content; for Vortex docs also run the owning
  documentation checker. Report what was checked and what was not run.
