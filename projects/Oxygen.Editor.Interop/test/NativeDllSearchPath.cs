// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Oxygen.Editor.Interop.Tests;

/// <summary>
/// Configures the Windows DLL loader for the mixed-mode interop assembly under test.
/// </summary>
/// <remarks>
/// The editor interop assembly intentionally runs against the Oxygen.Engine install
/// layout. The tests should therefore load native engine DLLs from
/// <c>projects/Oxygen.Engine/out/install/{Configuration}/bin</c>, not copy those DLLs
/// into the managed test project output.
/// </remarks>
internal static partial class NativeDllSearchPath
{
    private const int LoadLibrarySearchSystem32 = 0x00000800;
    private const int LoadLibrarySearchUserDirs = 0x00000400;

    /// <summary>
    /// Registers native DLL directories before any test touches <c>DroidNet.Oxygen.Editor.Interop.dll</c>.
    /// </summary>
    [ModuleInitializer]
    internal static void Configure()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        EnsureWindowsDllSearchMode();
        AddDirectory(ResolveEngineBinDirectory());
    }

    private static void EnsureWindowsDllSearchMode()
    {
        if (SetDefaultDllDirectories(LoadLibrarySearchSystem32 | LoadLibrarySearchUserDirs))
        {
            return;
        }

        throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to configure the process DLL search mode.");
    }

    private static void AddDirectory(string path)
    {
        if (!Directory.Exists(path))
        {
            throw new DirectoryNotFoundException($"Native dependency directory '{path}' does not exist.");
        }

        var cookie = AddDllDirectory(path);
        if (cookie == IntPtr.Zero)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"Failed to add native dependency directory '{path}'.");
        }

        // The directory remains registered for the lifetime of the process unless
        // RemoveDllDirectory is called. The tests do not need to unregister it.
    }

    private static string ResolveEngineBinDirectory()
    {
        var repositoryRoot = FindRepositoryRoot();
        var configuration = ResolveConfiguration();
        return Path.Combine(repositoryRoot.FullName, "projects", "Oxygen.Engine", "out", "install", configuration, "bin");
    }

    private static DirectoryInfo FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            var installRoot = Path.Combine(directory.FullName, "projects", "Oxygen.Engine", "out", "install");
            if (Directory.Exists(installRoot))
            {
                return directory;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the DroidNet repository root from the test output directory.");
    }

    private static string ResolveConfiguration()
    {
        var outputDirectory = new DirectoryInfo(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
        return outputDirectory.Name.StartsWith("Release", StringComparison.OrdinalIgnoreCase) ? "Release" : "Debug";
    }

    [LibraryImport("kernel32.dll", EntryPoint = "SetDefaultDllDirectories", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetDefaultDllDirectories(int directoryFlags);

    [LibraryImport("kernel32.dll", EntryPoint = "AddDllDirectory", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial IntPtr AddDllDirectory(string newDirectory);
}
