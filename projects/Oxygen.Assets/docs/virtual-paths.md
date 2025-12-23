# Virtual Paths (Single Source of Truth)

This document defines the canonical rules for **virtual paths** across Oxygen tooling and runtime.

Virtual paths are **editor-facing identity strings** (what scenes/prefabs persist). Runtime loads assets by `AssetKey`, so the pipeline must maintain a deterministic mapping `VirtualPath ↔ AssetKey`.

## Definition

A **virtual path** is a canonical, normalized string of the form:

- `/{MountPoint}/{Path}`

Examples:

- `/Content/Textures/Wood.otex`
- `/Engine/Materials/Debug.omat`

The corresponding canonical asset URI form is:

- `asset://{MountPoint}/{Path}`

Example:

- `/Content/Textures/Wood.otex` ⇔ `asset://Content/Textures/Wood.otex`

## Canonicalization and validation rules (required)

These rules are aligned with runtime validation (see `Oxygen.Content` virtual-path resolution).

A valid virtual path:

- Must not be empty.
- Must start with `/`.
- Must use `/` as the separator (never `\\`).
- Must not end with `/` (except the root `/`).
- Must not contain `//`.
- Must not contain `.` or `..` path segments.

Normalization guidance:

- When constructing paths from OS file paths, always convert separators to `/`.
- When joining segments, never introduce empty segments.

## Case policy

Virtual paths are identity. The case policy must be explicit and consistent.

Recommended policy:

- Treat virtual paths as **case-sensitive** for identity.
- Handle platform case quirks in UI and filesystem adapters, not by silently rewriting identity.

## Mount point expectations

- The first path segment after the leading `/` is the mount point (e.g. `Content`, `Engine`).
- Mount point names should be treated as case-sensitive tokens.

## Diagnostics and collision handling

- Virtual-path-to-`AssetKey` lookup is deterministic.
- When the same virtual path is found in multiple mounted sources:
  - First-match wins for resolution.
  - Collisions that map to different `AssetKey` values should produce a warning diagnostic (so users can resolve ambiguity).

## References

- Runtime validation is enforced by the Content subsystem’s virtual-path resolver implementation.
- Several runtime docs previously repeated these rules; they should now reference this document.
