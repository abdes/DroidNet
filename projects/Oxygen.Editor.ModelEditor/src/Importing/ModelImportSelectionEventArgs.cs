// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The files the import dialog selects itself, for the list to show as its selection.</summary>
/// <param name="files">The selected files, in the list's order.</param>
public sealed class ModelImportSelectionEventArgs(IReadOnlyList<ModelImportFile> files) : EventArgs
{
    /// <summary>Gets the selected files, in the list's order.</summary>
    public IReadOnlyList<ModelImportFile> Files { get; } = files;
}
