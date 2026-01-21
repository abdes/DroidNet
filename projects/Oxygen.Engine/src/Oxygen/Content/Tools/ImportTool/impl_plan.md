# ImportTool Implementation Plan (Junior‑Friendly)

## Overview

This plan translates the ImportTool README requirements into executable phases. Each phase ends with concrete deliverables and validation checks. Follow phases in order to avoid rework.

## Phase 0 — Orientation and Baseline

**Goal:** Ensure you understand the scope and locate the implementation anchors.

1. Read the specification in [src/Oxygen/Content/Tools/ImportTool/README.md](src/Oxygen/Content/Tools/ImportTool/README.md).
2. Identify the current implementation files:
   - [src/Oxygen/Content/Tools/ImportTool/TextureCommand.cpp](src/Oxygen/Content/Tools/ImportTool/TextureCommand.cpp)
   - [src/Oxygen/Content/Tools/ImportTool/BatchCommand.cpp](src/Oxygen/Content/Tools/ImportTool/BatchCommand.cpp)
   - [src/Oxygen/Content/Tools/ImportTool/ImportRunner.cpp](src/Oxygen/Content/Tools/ImportTool/ImportRunner.cpp)
3. Review relevant modules for mapping and constraints:
   - [src/Oxygen/Content/Import/ImportRequest.h](src/Oxygen/Content/Import/ImportRequest.h)
   - [src/Oxygen/Content/Import/ImportOptions.h](src/Oxygen/Content/Import/ImportOptions.h)
   - [src/Oxygen/Content/Import/AsyncImportService.h](src/Oxygen/Content/Import/AsyncImportService.h)
   - [src/Oxygen/Content/Import/ImportManifest.h](src/Oxygen/Content/Import/ImportManifest.h)
   - [src/Oxygen/Content/Import/ImportManifest_schema.h](src/Oxygen/Content/Import/ImportManifest_schema.h)
   - [src/Oxygen/Content/Import/ImportReport.h](src/Oxygen/Content/Import/ImportReport.h)

**Deliverables:**

- Short notes on how current CLI options map to `ImportRequest` and `ImportOptions`.

**Validation:**

- Confirm which commands already exist and which are planned.

**Phase 0 Findings (Completed):**

- Clap is a C++ library; global options must be registered via
   `CliBuilder::WithGlobalOptions` or `WithGlobalOption` and appear before
   commands in help output.
- Built-in help/version are enabled through `CliBuilder::WithHelpCommand()`
   and `CliBuilder::WithVersionCommand()`; help is printed during parsing.
- Theme values supported by Clap are `plain`, `dark`, and `light`.
- Bool options in Clap are flags with implicit `true` values by default.
- ImportTool currently has `texture` and `batch` commands only; `fbx` and
   `gltf` are not implemented yet.

---

## Phase 1 — CLI Stabilization and Global Options

**Goal:** Implement the full CLI surface and global options described in the README.

1. Add missing global options to the Clap CLI entry point (see README section 4.1).
2. Enforce global‑before‑command parsing rules.
3. Ensure option precedence rules are followed (README section 4.1).
4. Ensure help output lists global options before command list.

**Deliverables:**

- CLI accepts all global options and shows them in help.
- Missing or invalid global options generate actionable errors.

**Validation:**

- Manual run: `--help` lists all globals.
- Manual run: invalid global value returns exit code 2 and message.

---

## Phase 2 — Texture Command Completion

**Goal:** Align `texture` command behavior with the README specification.

1. Verify all texture options exist and map to `TextureImportSettings` and `ImportOptions::texture_tuning`.
2. Implement or verify defaults for intent, color space, output format, and mip settings.
3. Validate conditional option behavior:
   - `--data-format` only when `--intent=data`.
   - `--bc7-quality` only for BC7 outputs.
   - `--max-mips` only with `--mip-policy=max`.
4. Ensure `--preset` behavior follows “preset first, then CLI overrides”.
5. Confirm output root resolution: `--output` or global `--cooked-root`.

**Deliverables:**

- `texture` command fully matches sections 4.2.x in the README.

**Validation:**

- Manual tests of valid/invalid option combinations.
- Confirm correct exit code 2 for missing output root.

---

## Phase 3 — FBX and glTF Commands

**Goal:** Implement new `fbx` and `gltf` commands per README.

1. Add `fbx` and `gltf` commands to CLI command list.
2. Map options to `ImportOptions` fields:
   - Content flags, unit policy, bake transforms.
   - Normals, tangents, and node pruning policies.
3. Implement glTF defaults (unit policy and coordinate conversion).
4. Ensure both commands set `ImportRequest::import_format` correctly.
5. Validate missing output root handling and exit codes.

**Deliverables:**

- `fbx` and `gltf` commands parse and validate options.
- Requests are constructed with correct formats and options.

**Validation:**

- Manual runs with valid options.
- Invalid values return exit code 2 with clear messages.

---

## Phase 4 — Manifest Evolution and Validation

**Goal:** Implement the manifest schema evolution and validation rules.

1. Update or confirm manifest model supports:
   - `version`, `defaults`, `jobs`, `job_type`.
   - per‑job overrides and dependencies.
2. Update schema definition in `ImportManifest_schema.h` to match README.
3. Ensure `ImportManifestLoader.h` enforces schema version and provides errors.
4. Add dependency validation for circular references and missing job IDs.
5. Confirm default resolution order matches CLI precedence rules.

**Deliverables:**

- Manifest format supports all fields in README section 5.
- Validation rejects bad schema versions, unknown job types, and cycles.

**Validation:**

- Create test manifests for good and bad cases; verify error code 4.

---

## Phase 5 — Execution Model and Scheduling

**Goal:** Align runtime execution with concurrency, fail‑fast, and dependency scheduling requirements.

1. Confirm `AsyncImportService` concurrency control is wired to `--max-in-flight`.
2. Implement dependency‑aware scheduling in batch mode.
3. Implement fail‑fast cancellation within 100ms.
4. Implement dry‑run mode for batch validation only.

**Deliverables:**

- Batch execution respects dependencies and concurrency limits.
- Fail‑fast terminates in‑flight jobs quickly.

**Validation:**

- Run manifest with dependencies and confirm ordering.
- Simulate failure and confirm cancellation behavior.

---

## Phase 6 — Progress Output and TUI

**Goal:** Implement user‑visible progress for TUI and non‑TUI modes.

1. Ensure TUI disable conditions are honored.
2. Implement structured text output format with timestamps when TUI is disabled.
3. Ensure progress callbacks report phases at least once per second for long jobs.

**Deliverables:**

- TUI works in interactive terminals; text mode works in CI.

**Validation:**

- Run batch in a non‑TTY and verify text output format.

---

## Phase 7 — Reporting Contract

**Goal:** Produce JSON reports matching README section 7.

1. Implement top‑level report structure using `ImportReport.h`.
2. Populate summary fields, per‑job entries, diagnostics, telemetry, and hashes.
3. Ensure assets and resources are reported according to `Oxygen::Data` metadata.
4. Ensure dependency lists include asset and resource references.

**Deliverables:**

- JSON report generation for `texture` and `batch`.

**Validation:**

- Use a sample job and verify report fields exist and are correct.

---

## Phase 8 — Exit Codes and Error Handling

**Goal:** Ensure exit code behavior matches README.

1. Map errors to exit codes consistently across commands.
2. Ensure invalid arguments return code 2; validation errors return code 4.
3. Ensure job failures return code 5 (unless fail‑fast).

**Deliverables:**

- All CLI commands return documented exit codes.

**Validation:**

- Manual tests for each error type.

---

## Phase 9 — Unit and Integration Tests

**Goal:** Provide tests for CLI parsing, manifest validation, and reporting.

1. Add CLI parsing tests for each command and option set.
2. Add manifest validation tests (schema, defaults, dependencies).
3. Add reporting tests to validate fields and schema version.

**Deliverables:**

- Tests matching README section 9 requirements.

**Validation:**

- All tests pass locally and in CI.

---

## Phase 10 — Documentation and Examples

**Goal:** Ensure the README stays accurate and complete.

1. Add or update example invocations for new commands.
2. Ensure exit codes and help text are documented.

**Deliverables:**

- README remains the authoritative spec.

**Validation:**

- Quick review against actual CLI behavior.
