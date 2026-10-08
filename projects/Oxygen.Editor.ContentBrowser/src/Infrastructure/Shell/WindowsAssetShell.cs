// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Windows.ApplicationModel.DataTransfer;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>Clipboard and File Explorer actions on Windows.</summary>
public sealed class WindowsAssetShell : IAssetShell
{
    /// <inheritdoc />
    public void CopyText(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        var package = new DataPackage();
        package.SetText(text);
        Clipboard.SetContent(package);
    }

    /// <inheritdoc />
    public bool ShowInFileExplorer(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var full = Path.GetFullPath(path);
        string arguments;
        if (File.Exists(full))
        {
            arguments = $"/select,\"{full}\"";
        }
        else if (Directory.Exists(full))
        {
            arguments = $"\"{full}\"";
        }
        else
        {
            return false;
        }

        using var process = Process.Start(new ProcessStartInfo("explorer.exe", arguments) { UseShellExecute = true });
        return true;
    }
}
