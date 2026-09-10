// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.World.Services;

/// <summary>Identifies the node whose interactive previews are throttled.</summary>
/// <param name="SceneId">The scene identity.</param>
/// <param name="NodeId">The node identity.</param>
[StructLayout(LayoutKind.Auto)]
internal readonly record struct SyncCoalescingKey(Guid SceneId, Guid NodeId);
