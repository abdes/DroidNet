# Headless Graphics Backend

This module provides a headless graphics backend for testing and simulation.

Purpose

- Emulate resource creation (buffers, textures, shaders) with minimal CPU-side state.
- Track resource state transitions and validate usage patterns.
- Simulate command recording and submission with deterministic fences.
- Record replayable logs in a later phase (JSON-based format).

Namespace: `oxygen::graphics::headless`
Module: `Oxygen.Graphics.Headless`

Build
This module is always built as part of the `Oxygen.Graphics` aggregation.

Phase 1: skeleton and validation (no rendering output).
Phase 2: deterministic replay format and replay loader.
