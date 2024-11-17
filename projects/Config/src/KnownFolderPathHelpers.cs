// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;

namespace DroidNet.Config;

/// <summary>
/// Provides helper methods for retrieving known folder paths.
/// </summary>
internal static class KnownFolderPathHelpers
{
    /// <summary>
    /// Retrieves the full path of a known folder identified by the folder's Id.
    /// </summary>
    /// <param name="refToGuid">A reference to the Id that identifies the folder.</param>
    /// <param name="dwFlags">Flags that specify special retrieval options.</param>
    /// <param name="hToken">
    /// An access token that represents a particular user. If this parameter is NULL, the function
    /// requests the known folder for the current user.
    /// </param>
    /// <returns>The path of the known folder.</returns>
    /// <exception cref="System.Runtime.InteropServices.COMException">
    /// Thrown when the method fails to retrieve the known folder path.
    /// </exception>
    [DllImport("shell32.dll", SetLastError = true, CharSet = CharSet.Unicode, PreserveSig = false)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [SuppressMessage(
        "Interoperability",
        "SYSLIB1054:Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time",
        Justification = "LibraryImport does not support marshalling of the Guid")]
    public static extern string SHGetKnownFolderPath(
        [MarshalAs(UnmanagedType.LPStruct)] Guid refToGuid,
        uint dwFlags,
        nint hToken = default);
}
