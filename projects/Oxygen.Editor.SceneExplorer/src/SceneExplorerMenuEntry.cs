// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// A single context-menu entry: the shared action identity, its label and whether it is currently available.
/// </summary>
/// <param name="Kind">The shared action identity.</param>
/// <param name="Label">The contextual display label.</param>
/// <param name="IsEnabled">Whether the action can currently be invoked.</param>
public sealed record SceneExplorerMenuEntry(SceneExplorerCommandKind Kind, string Label, bool IsEnabled);
