// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Commands;

/// <summary>A command that parts handle and the window frame binds to keys.</summary>
/// <param name="Id">The command's identity, unique in the editor, such as <c>Edit.Undo</c>.</param>
/// <param name="Label">What menus and tooltips call the command.</param>
public sealed record CommandDescriptor(string Id, string Label)
{
    /// <summary>Gets the group the command belongs to, such as <c>Edit</c>.</summary>
    public string Category { get; init; } = "General";

    /// <summary>Gets the keys that run the command by default.</summary>
    public IReadOnlyList<CommandKeyGesture> KeyGestures { get; init; } = [];
}
