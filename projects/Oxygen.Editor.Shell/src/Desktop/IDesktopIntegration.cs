// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>The desktop actions parts hand to the operating system: the clipboard, File Explorer and the Recycle Bin.</summary>
public interface IDesktopIntegration
{
    /// <summary>Places text on the clipboard.</summary>
    /// <param name="text">The text to copy.</param>
    public void CopyText(string text);

    /// <summary>Opens File Explorer with a file selected, or a folder open.</summary>
    /// <param name="path">The absolute file or folder path.</param>
    /// <returns>False when the path no longer exists.</returns>
    public bool ShowInFileExplorer(string path);

    /// <summary>Moves files and folders to the Recycle Bin.</summary>
    /// <param name="paths">The absolute paths.</param>
    /// <exception cref="IOException">The shell could not recycle every path.</exception>
    public void MoveToRecycleBin(IReadOnlyList<string> paths);
}
