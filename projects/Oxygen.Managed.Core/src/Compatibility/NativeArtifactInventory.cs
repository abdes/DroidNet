// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Locates only the dependencies needed by the requested native operation.</summary>
public static class NativeArtifactInventory
{
    /// <summary>The editor's mixed-mode bridge.</summary>
    public const string InteropId = "editor/DroidNet.Oxygen.Editor.Interop.dll";

    /// <summary>The native import tool.</summary>
    public const string ImportToolId = "engine/bin/Oxygen.Cooker.ImportTool.exe";

    /// <summary>Builds the startup inventory without requiring cooker tools or editor UI binaries.</summary>
    /// <param name="installation">The editor and SDK installation.</param>
    /// <returns>The runtime's required files.</returns>
    public static ImmutableArray<NativeArtifactLocation> CreateRuntime(EditorNativeInstallation installation)
    {
        var files = NativeLibraries(installation);
        files[InteropId] = new(InteropId, installation.InteropPath);
        return [.. files.Values.OrderBy(static value => value.Id, StringComparer.Ordinal)];
    }

    /// <summary>Builds the cooking inventory independently of Interop and runtime startup.</summary>
    /// <param name="installation">The editor and SDK installation.</param>
    /// <returns>The tools, schemas and producer assemblies used by cooking.</returns>
    public static ImmutableArray<NativeArtifactLocation> CreateCooking(EditorNativeInstallation installation)
    {
        var files = NativeLibraries(installation);
        files[ImportToolId] = new(ImportToolId, Path.Combine(installation.EngineRoot, "bin", "Oxygen.Cooker.ImportTool.exe"));
        foreach (var name in new[] { "oxygen.scene-descriptor.schema.json", "oxygen.material-descriptor.schema.json", "oxygen.geometry-descriptor.schema.json", "oxygen.import-manifest.schema.json", "oxygen.buffer-container.schema.json", "oxygen.scene-source-inspection.schema.json" })
        {
            AddSchema("engine/schemas/" + name, Path.Combine(installation.EngineRoot, "schemas", name));
            AddSchema("editor/Schemas/" + name, Path.Combine(installation.EditorRoot, "Schemas", name));
        }

        foreach (var name in new[] { "Oxygen.Editor.ContentPipeline.dll", "Oxygen.Editor.Schemas.dll", "Oxygen.Editor.World.dll", "Oxygen.Managed.Assets.dll", "Oxygen.Managed.Core.dll" })
        {
            var id = "editor/" + name;
            files[id] = new(id, Path.Combine(installation.EditorRoot, name));
        }

        return [.. files.Values.OrderBy(static value => value.Id, StringComparer.Ordinal)];

        void AddSchema(string id, string path)
        {
            string? schemaId = null;
            if (File.Exists(path))
            {
                using var document = JsonDocument.Parse(File.ReadAllBytes(path));
                schemaId = document.RootElement.TryGetProperty("$id", out var value) ? value.GetString() : Path.GetFileName(path);
            }

            files[id] = new(id, path, schemaId);
        }
    }

    /// <summary>Gets the native runtime identity for a build configuration.</summary>
    /// <param name="configuration">Debug or Release.</param>
    /// <returns>The runtime file's portable identity.</returns>
    public static string RuntimeId(string configuration) => "engine/bin/Oxygen.Engine" + (string.Equals(configuration, "Debug", StringComparison.Ordinal) ? "-d" : string.Empty) + ".dll";

    private static Dictionary<string, NativeArtifactLocation> NativeLibraries(EditorNativeInstallation installation)
    {
        var files = new Dictionary<string, NativeArtifactLocation>(StringComparer.Ordinal);
        var bin = Path.Combine(installation.EngineRoot, "bin");
        if (Directory.Exists(bin))
        {
            foreach (var path in Directory.EnumerateFiles(bin, "*.dll"))
            {
                var id = "engine/bin/" + Path.GetFileName(path);
                files[id] = new(id, path);
            }
        }

        foreach (var id in new[] { RuntimeId(installation.Configuration), "engine/bin/Oxygen.Engine.EditorInterface" + (string.Equals(installation.Configuration, "Debug", StringComparison.Ordinal) ? "-d" : string.Empty) + ".dll" })
        {
            files[id] = new(id, Path.Combine(bin, Path.GetFileName(id)));
        }

        return files;
    }
}
