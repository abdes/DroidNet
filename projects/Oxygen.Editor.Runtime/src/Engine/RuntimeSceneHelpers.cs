// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The workspace state the editing views' light and camera helpers follow.</summary>
/// <param name="HiddenNodeIds">Workspace-hidden nodes: no icon or helper, and not pickable.</param>
/// <param name="LockedNodeIds">Locked nodes: their helpers' handles cannot be dragged.</param>
/// <param name="DisplayScale">Physical pixels per device-independent pixel, which sizes icons and handles.</param>
public sealed record RuntimeSceneHelpers(
    IReadOnlyList<Guid> HiddenNodeIds,
    IReadOnlyList<Guid> LockedNodeIds,
    float DisplayScale);
