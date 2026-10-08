// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser;

/// <summary>The desktop actions the Content Browser hands to the operating system.</summary>
public interface IAssetShell
{
    /// <summary>Places text on the clipboard.</summary>
    /// <param name="text">The text to copy.</param>
    public void CopyText(string text);

    /// <summary>Opens File Explorer with a file selected, or a folder open.</summary>
    /// <param name="path">The absolute file or folder path.</param>
    /// <returns>False when the path no longer exists.</returns>
    public bool ShowInFileExplorer(string path);
}
