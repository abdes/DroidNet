//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Modules/TransformsModule.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine {

/*!
 Executes the scene transform propagation phase.

 Flow
 1. Acquire non-owning scene pointer from the frame context.
 2. If absent: early return (benign no-op, keeps frame deterministic).
 3. Call Scene::Update() which performs:
    - Pass 1: Dense linear scan processing dirty node flags (non-transform).
    - Pass 2: Pre-order filtered traversal (DirtyTransformFilter) resolving
      world transforms only along dirty chains (parent first).
 4. Return; no extra state retained by this module.

 Invariants / Guarantees
 - Invoked exactly once per frame in kTransformPropagation phase.
 - Parent world matrix valid before any child transform recompute.
 - Clean descendants of a dirty ancestor incur only an early-out check.
 - kIgnoreParentTransform subtrees intentionally skipped per design.
 - No scene graph structural mutation occurs here.
 - No GPU resource mutation or uploads here (CPU authoritative only).

 Never Do
 - Do not reparent / create / destroy nodes here.
 - Do not call Scene::Update() more than once per frame.
 - Do not cache raw pointers across frames.
 - Do not allocate large transient buffers (Scene owns traversal memory).
 - Do not introduce side-effects dependent on sibling visitation order.

 Performance Characteristics
 - Time: O(F + T) where F = processed dirty flags, T = visited transform
   chain nodes (<= total nodes, typically sparse).
 - Memory: No steady-state allocations.
 - Optimization: Early-exit for clean transforms; dense flag pass for cache
 locality.

 Future Improvement (Parallel Chains)
 - The scene's root hierarchies are independent for transform propagation.
 - A future optimization can collect the subset of root hierarchies that have
   at least one dirty descendant and dispatch each qualifying root subtree to
   a worker task (parent-first order preserved inside each task, no sharing).
 - Synchronize (join) all tasks before proceeding to later phases to maintain
   frame determinism. Skip parallel dispatch below a configurable dirty-node
   threshold to avoid overhead on small scenes.
 - This preserves all existing invariants (no graph mutation, parent-first,
   single update per node) while offering scalable speedups on large scenes.

 @note Dirty flag semantics, traversal filtering, and no-mutation policy are
       deliberate and should be preserved.
 @see oxygen::scene::Scene::Update
 @see oxygen::scene::SceneTraversal::UpdateTransforms
 @see oxygen::scene::DirtyTransformFilter
*/
auto TransformsModule::OnTransformPropagation(FrameContext& context) -> co::Co<>
{
  // Acquire scene pointer (non-owning). If absent, log once per frame in debug.
  auto scene_ptr = context.GetScene();
  if (!scene_ptr) {
    DLOG_F(
      1, "TransformsModule: no active scene set in FrameContext (skipping)");
    co_return; // Nothing to update
  }

  auto saved = loguru::g_stderr_verbosity;
  loguru::g_stderr_verbosity
    = 0; // FIXME: Suppress noisy logs from Scene::Update
  // Perform hierarchy propagation & world matrix updates.
  scene_ptr->Update();
  loguru::g_stderr_verbosity = saved; // Restore original verbosity
  co_return;
}

} // namespace oxygen::engine
