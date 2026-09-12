// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Defines the editor/native/tool/schema inventory independently of the accepted manifest.</summary>
public static class EditorArtifactInventory
{
    /// <summary>The mixed-mode assembly used by the editor.</summary>
    public const string InteropId = "editor/DroidNet.Oxygen.Editor.Interop.dll";

    /// <summary>The native content import tool.</summary>
    public const string ImportToolId = "engine/bin/Oxygen.Cooker.ImportTool.exe";

    /// <summary>The standalone renderer used for qualification.</summary>
    public const string RenderSceneId = "engine/bin/Oxygen.Examples.RenderScene.exe";

    /// <summary>Creates the complete expected inventory, retaining mandatory entries even when absent.</summary>
    /// <param name="installation">The application and SDK locations.</param>
    /// <returns>The required artifact paths and schema identities.</returns>
    public static ImmutableArray<QualificationArtifactLocation> Create(EditorArtifactInstallation installation)
    {
        ArgumentNullException.ThrowIfNull(installation);
        var files = new Dictionary<string, QualificationArtifactLocation>(StringComparer.Ordinal);
        foreach (var name in new[] { "Oxygen.Editor.exe", "Oxygen.Editor.dll", "DroidNet.Oxygen.Editor.Interop.dll", "Oxygen.Editor.Runtime.dll", "Oxygen.Editor.ContentPipeline.dll", "Oxygen.Editor.Schemas.dll", "Oxygen.Managed.Core.dll", "Oxygen.Managed.Assets.dll", "Oxygen.Editor.World.dll" })
        {
            Add("editor", installation.EditorRoot, name);
        }

        var suffix = string.Equals(installation.Configuration, "Debug", StringComparison.Ordinal) ? "-d" : string.Empty;
        foreach (var name in new[] { "Oxygen.Engine" + suffix + ".dll", "Oxygen.Engine.EditorInterface" + suffix + ".dll", "Oxygen.Cooker.ImportTool.exe", "Oxygen.Cooker.Inspector.exe", "Oxygen.Examples.RenderScene.exe" })
        {
            Add("engine", installation.EngineRoot, "bin/" + name);
        }

        foreach (var name in new[] { "oxygen.scene-descriptor.schema.json", "oxygen.material-descriptor.schema.json", "oxygen.geometry-descriptor.schema.json", "oxygen.import-manifest.schema.json", "oxygen.buffer-container.schema.json" })
        {
            Add("engine", installation.EngineRoot, "schemas/" + name, schema: true);
            Add("editor", installation.EditorRoot, "Schemas/" + name, schema: true);
        }

        foreach (var name in new[] { "oxygen.scene-descriptor.editor.schema.json", "oxygen.material-descriptor.editor.schema.json", "oxygen.transform-component.schema.json", "oxygen.transform-component.editor.schema.json" })
        {
            Add("editor", installation.EditorRoot, "Schemas/" + name, schema: true);
        }

        Enumerate(
            "editor",
            installation.EditorRoot,
            string.Empty,
            recursive: true,
            static path => !path.StartsWith("Engine/", StringComparison.OrdinalIgnoreCase)
                && (path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)
                    || path.EndsWith(".xbf", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".pri", StringComparison.OrdinalIgnoreCase)
                    || path.EndsWith(".deps.json", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".runtimeconfig.json", StringComparison.OrdinalIgnoreCase)
                    || path.StartsWith("Schemas/", StringComparison.OrdinalIgnoreCase)));
        Enumerate("engine", installation.EngineRoot, "bin", recursive: false, static path => path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase));
        Enumerate("engine", installation.EngineRoot, "schemas", recursive: true, static _ => true);
        return [.. files.Values.OrderBy(static artifact => artifact.Id, StringComparer.Ordinal)];

        void Add(string owner, string root, string relative, bool schema = false)
        {
            relative = relative.Replace('\\', '/');
            var path = Path.GetFullPath(Path.Combine(root, relative));
            var id = owner + "/" + relative;
            var schemaId = schema || relative.Contains("Schemas/", StringComparison.OrdinalIgnoreCase) ? ReadSchemaId(path) : null;
            files[id] = new(id, path, schemaId);
        }

        void Enumerate(string owner, string root, string relativeDirectory, bool recursive, Func<string, bool> include)
        {
            var directory = Path.Combine(root, relativeDirectory);
            if (!Directory.Exists(directory))
            {
                return;
            }

            foreach (var path in Directory.EnumerateFiles(directory, "*", recursive ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly))
            {
                var relative = Path.GetRelativePath(root, path).Replace('\\', '/');
                if (include(relative))
                {
                    Add(owner, root, relative);
                }
            }
        }
    }

    /// <summary>Gets the mandatory native runtime artifact identity for a build configuration.</summary>
    /// <param name="configuration">The build configuration.</param>
    /// <returns>The runtime DLL's portable identity.</returns>
    public static string RuntimeId(string configuration) => "engine/bin/Oxygen.Engine" + (string.Equals(configuration, "Debug", StringComparison.Ordinal) ? "-d" : string.Empty) + ".dll";

    private static string ReadSchemaId(string path)
    {
        if (!File.Exists(path))
        {
            return Path.GetFileName(path);
        }

        using var document = JsonDocument.Parse(File.ReadAllBytes(path));
        return document.RootElement.TryGetProperty("$id", out var id) && id.ValueKind == JsonValueKind.String && id.GetString() is { Length: > 0 } value ? value : Path.GetFileName(path);
    }
}
