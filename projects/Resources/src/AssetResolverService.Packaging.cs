// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Resources;

/// <summary>
///     Whether the application runs packaged, for <see cref="AssetResolverService" />.
/// </summary>
/// <remarks>
///     The process's package identity is asked once, from Windows, without the exception that
///     <see cref="Windows.ApplicationModel.Package.Current" /> throws in an unpackaged application.
/// </remarks>
public partial class AssetResolverService
{
    // Returned by GetCurrentPackageFullName for a process that has no package identity.
    private const int AppModelErrorNoPackage = 15700;

    private static readonly Lazy<bool> Packaged = new(HasPackageIdentity);

    private static bool IsPackaged => Packaged.Value;

    private static bool HasPackageIdentity()
    {
        uint length = 0;
        return GetCurrentPackageFullName(ref length, IntPtr.Zero) != AppModelErrorNoPackage;
    }

    [LibraryImport("kernel32.dll", EntryPoint = "GetCurrentPackageFullName")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial int GetCurrentPackageFullName(ref uint packageFullNameLength, IntPtr packageFullName);
}
