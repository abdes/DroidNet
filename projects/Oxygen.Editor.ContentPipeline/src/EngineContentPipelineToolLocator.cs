// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Repository-local locator for Oxygen Engine content-pipeline tools.
/// </summary>
public sealed class EngineContentPipelineToolLocator : IEngineContentPipelineToolLocator
{
    private const string ImportToolExecutableName = "Oxygen.Cooker.ImportTool.exe";
    private const string ImportToolEnvironmentVariable = "OXYGEN_IMPORT_TOOL";

    /// <inheritdoc />
    public string GetImportToolPath()
    {
        var fromEnvironment = Environment.GetEnvironmentVariable(ImportToolEnvironmentVariable);
        if (!string.IsNullOrWhiteSpace(fromEnvironment) && File.Exists(fromEnvironment))
        {
            return fromEnvironment;
        }

        foreach (var candidate in EnumerateCandidates())
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        throw new FileNotFoundException(
            $"Could not locate {ImportToolExecutableName}. Set {ImportToolEnvironmentVariable} or build/install Oxygen.Engine.");
    }

    private static IEnumerable<string> EnumerateCandidates()
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
            var engineRoot = Path.Combine(current.FullName, "projects", "Oxygen.Engine");
            yield return Path.Combine(engineRoot, "out", "install", configuration, "bin", ImportToolExecutableName);
            yield return Path.Combine(engineRoot, "out", "build-vs", "bin", configuration, ImportToolExecutableName);
            yield return Path.Combine(engineRoot, "out", "build-ninja", "bin", configuration, ImportToolExecutableName);

            current = current.Parent;
        }
    }
}
