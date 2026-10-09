// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
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

    /// <inheritdoc />
    public void MoveToRecycleBin(IReadOnlyList<string> paths)
    {
        ArgumentNullException.ThrowIfNull(paths);
        if (paths.Count == 0)
        {
            return;
        }

        // SHFileOperation takes a list of paths separated and terminated by NUL characters. A path the Recycle Bin
        // cannot take, such as one on a network share, would be deleted permanently without the nuke warning, so the
        // shell asks first; declining aborts the operation.
        var operation = new NativeMethods.ShFileOperation
        {
            Function = NativeMethods.FileOperationDelete,
            From = string.Join('\0', paths.Select(Path.GetFullPath)) + "\0\0",
            Flags = (ushort)(NativeMethods.AllowUndo | NativeMethods.NoConfirmation | NativeMethods.Silent | NativeMethods.NoErrorUi | NativeMethods.WantNukeWarning),
        };
        var result = NativeMethods.SHFileOperation(ref operation);
        if (operation.AnyOperationsAborted)
        {
            throw new IOException("Moving to the Recycle Bin was cancelled; some items may already be in the Recycle Bin.");
        }

        if (result != 0)
        {
            throw new IOException(string.Create(CultureInfo.InvariantCulture, $"The files could not be moved to the Recycle Bin (shell error 0x{result:X})."));
        }
    }

    private static class NativeMethods
    {
        public const uint FileOperationDelete = 3;
        public const ushort AllowUndo = 0x40;
        public const ushort NoConfirmation = 0x10;
        public const ushort Silent = 0x4;
        public const ushort NoErrorUi = 0x400;
        public const ushort WantNukeWarning = 0x4000;

        [System.Runtime.InteropServices.DllImport("shell32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, EntryPoint = "SHFileOperationW")]
        [System.Runtime.InteropServices.DefaultDllImportSearchPaths(System.Runtime.InteropServices.DllImportSearchPath.System32)]
        public static extern int SHFileOperation(ref ShFileOperation operation);

        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        public struct ShFileOperation
        {
            public nint Window;
            public uint Function;
            public string From;
            public string? To;
            public ushort Flags;
            [System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.Bool)]
            public bool AnyOperationsAborted;
            public nint NameMappings;
            public string? ProgressTitle;
        }
    }
}
