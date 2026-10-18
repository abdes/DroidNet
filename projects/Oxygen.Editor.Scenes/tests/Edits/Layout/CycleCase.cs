// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>A folder moved into a place inside itself.</summary>
/// <param name="Moved">The folder that moves.</param>
/// <param name="Into">The folder that receives it: the same folder or one of its descendants.</param>
public sealed record CycleCase(FolderId Moved, FolderId Into);
