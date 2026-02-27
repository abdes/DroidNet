//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

//! Oxygen PAK file binary format specification umbrella include.
/*!
 @file PakFormat.h

 This is the single include entry point for the full PAK schema.
 Domain schemas are organized in `PakFormat_<domain>.h` files.

 ## PakFormat Style / Design Guidelines
 - This header set is the single source of truth for the current schema.
   Oxygen follows a latest-only policy; no legacy versioned namespaces are
   modeled here.
 - Every serialized struct is authored for deterministic binary layout.
   Use packed layout and keep explicit `static_assert(sizeof(...))` checks.
 - Offsets are absolute unless a field explicitly documents a local/table
   relative offset.
 - Domain ownership is explicit: each type/constant belongs to one
   `oxygen::data::pak::<domain>` namespace.
 - References across domains must use fully qualified symbols; avoid implicit
   namespace imports in schema headers.
 - Add fields only when they are semantically required. Do not add placeholder
   fields unless they are required to enforce a fixed-size binary contract.

 ## Predefined Domains And Intent
 - `core`:
   Foundational container and shared schema primitives (header/footer, resource
   regions/tables, asset header, canonical scalar/index/string-offset types).
 - `world`:
   Scene graph ownership and world-authored component records (nodes, renderable
   bindings, cameras, lights, environment systems).
 - `geometry`:
   Geometry asset descriptors and mesh/submesh/view records.
 - `render`:
   Material descriptors, shader references, texture resource/payload contracts.
 - `scripting`:
   Script assets/resources and scene script component bindings.
 - `input`:
   Input action/mapping assets and scene input-context bindings.
 - `physics`:
   Physics assets/resources and world-node physics sidecar bindings.
 - `audio`:
   Audio resource descriptors.
 - `animation`:
   Reserved animation domain namespace for explicit schema ownership.

 ## Dependency Direction
 - Allowed base dependency is `domain -> core`.
 - Scene-attached domains may additionally depend on `world`:
   `scripting -> world`, `input -> world`, `physics -> world`.
 - `core` must not depend on non-core domains.
*/

#include <Oxygen/Data/PakFormat_animation.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
