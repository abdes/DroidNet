// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>A selection to capture and the place to paste it.</summary>
/// <param name="Nodes">The selected nodes.</param>
/// <param name="Folders">The selected folders.</param>
/// <param name="Target">Where the selection is pasted.</param>
public sealed record PasteCase(IReadOnlyList<NodeId> Nodes, IReadOnlyList<FolderId> Folders, PasteTarget Target);
