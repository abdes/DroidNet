// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Asks a scene's editor to frame nodes in its focused viewport, as when a Scene Explorer row is
/// double-clicked.
/// </summary>
/// <param name="DocumentId">The scene document owning the nodes.</param>
/// <param name="NodeIds">The nodes to frame, with their descendants.</param>
internal sealed record FrameSceneNodesRequestMessage(Guid DocumentId, IReadOnlyList<Guid> NodeIds);
