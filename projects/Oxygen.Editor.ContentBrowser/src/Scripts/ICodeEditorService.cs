// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Opens scripts and input files in the user's code editor.
/// </summary>
/// <remarks>
/// The first open asks which editor to use and remembers the answer; <see cref="OpenWithAsync"/> asks again.
/// A launch failure is published as an operation result naming the command.
/// </remarks>
public interface ICodeEditorService
{
    /// <summary>Opens a file in the configured code editor, asking for one on first use.</summary>
    /// <param name="path">The file to open.</param>
    /// <param name="line">The 1-based line to show, if any.</param>
    /// <param name="column">The 1-based column to show, if any.</param>
    /// <returns><see langword="true"/> when the editor started; <see langword="false"/> when the user cancelled or
    /// the launch failed.</returns>
    public Task<bool> OpenAsync(string path, int? line = null, int? column = null);

    /// <summary>Asks which code editor to use, then opens the file with it.</summary>
    /// <param name="path">The file to open.</param>
    /// <returns><see langword="true"/> when the editor started.</returns>
    public Task<bool> OpenWithAsync(string path);
}
