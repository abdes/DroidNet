// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Identifies the property target whose interactive previews are throttled.</summary>
/// <param name="SceneId">The scene identity.</param>
/// <param name="PreviewTarget">The edited property target: a node's id, or the scene's id for its environment.</param>
[StructLayout(LayoutKind.Auto)]
internal readonly record struct SyncCoalescingKey(SceneId SceneId, Guid PreviewTarget);
