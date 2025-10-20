// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Config.Helpers;

/// <summary>
///     Provides helper methods for retrieving known folder paths.
/// </summary>
internal static partial class KnownFolders
{
    /// <summary>
    ///     Retrieves the full path of a known folder identified by the folder's GUID.
    /// </summary>
    /// <param name="folderId">The GUID that identifies the folder.</param>
    /// <param name="flags">Flags that specify special retrieval options.</param>
    /// <param name="hToken">
    ///     An access token that represents a particular user. If this parameter is NULL, the function
    ///     requests the known folder for the current user.
    /// </param>
    /// <returns>The path of the known folder.</returns>
    /// <exception cref="System.Runtime.InteropServices.COMException">
    ///     Thrown when the method fails to retrieve the known folder path.
    /// </exception>
    public static string GetKnownFolderPath(Guid folderId, uint flags = 0, nint hToken = 0)
    {
        var hr = SHGetKnownFolderPath(in folderId, flags, hToken, out var ppszPath);
        if (hr != 0)
        {
            Marshal.ThrowExceptionForHR(hr);
        }

        try
        {
            // Convert PWSTR to managed string
            return Marshal.PtrToStringUni(ppszPath) ?? string.Empty;
        }
        finally
        {
            // SHGetKnownFolderPath uses CoTaskMem; free with FreeCoTaskMem
            Marshal.FreeCoTaskMem(ppszPath);
        }
    }

    [LibraryImport("shell32.dll", SetLastError = true, EntryPoint = "SHGetKnownFolderPath")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    internal static partial int SHGetKnownFolderPath(
        in Guid rfid, // REFKNOWNFOLDERID (const GUID*)
        uint dwFlags, // DWORD
        nint hToken, // HANDLE (use 0 for current user)
        out nint ppszPath); // PWSTR* (allocated; must be freed)
}
