// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The workspace state the editing views' light and camera helpers follow.</summary>
/// <param name="HiddenNodes">
/// Workspace-hidden nodes, which may be nodes an instance brought in: no icon or helper for them or their descendants,
/// and not pickable.
/// </param>
/// <param name="LockedNodes">Locked nodes, which may be nodes an instance brought in: their helpers' handles cannot be dragged.</param>
/// <param name="DisplayScale">Physical pixels per device-independent pixel, which sizes icons and handles.</param>
public sealed record RuntimeSceneHelpers(
    IReadOnlyList<NodeReference> HiddenNodes,
    IReadOnlyList<NodeReference> LockedNodes,
    float DisplayScale);
