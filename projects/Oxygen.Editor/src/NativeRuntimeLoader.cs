// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

/// <summary>
///     Registers native runtime directories needed by the embedded Oxygen Engine.
/// </summary>
internal static class NativeRuntimeLoader
{
    /// <summary>
    ///     Adds the repository-local Oxygen Engine install bin directory to the process DLL search path.
    /// </summary>
    public static void RegisterEngineRuntimeDirectory()
    {
        var engineRuntimeDirectory = ResolveEngineRuntimeDirectory();
        var path = Environment.GetEnvironmentVariable("PATH");

        if (path?.Split(Path.PathSeparator).Contains(engineRuntimeDirectory, StringComparer.OrdinalIgnoreCase) != true)
        {
            var updatedPath = string.IsNullOrEmpty(path)
                ? engineRuntimeDirectory
                : string.Concat(engineRuntimeDirectory, Path.PathSeparator, path);

            Environment.SetEnvironmentVariable("PATH", updatedPath);
        }
    }

    private static string ResolveEngineRuntimeDirectory()
    {
        var configuration =
#if DEBUG
            "Debug";
#else
            "Release";
#endif

        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            var installRoot = Path.Combine(
                current.FullName,
                "projects",
                "Oxygen.Engine",
                "out",
                "install",
                configuration);

            foreach (var candidate in new[]
            {
                Path.Combine(installRoot, "bin"),
                Path.Combine(installRoot, "Oxygen"),
            })
            {
                if (Directory.Exists(candidate))
                {
                    return candidate;
                }
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            $"Could not locate Oxygen Engine native runtime directory for configuration '{configuration}' from '{AppContext.BaseDirectory}'. Expected a repo-local path like 'projects\\Oxygen.Engine\\out\\install\\{configuration}\\bin' or 'projects\\Oxygen.Engine\\out\\install\\{configuration}\\Oxygen'.");
    }
}
