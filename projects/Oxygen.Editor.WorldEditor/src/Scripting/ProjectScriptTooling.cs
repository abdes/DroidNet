// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Scripting;

/// <summary>
/// Keeps a project's script tooling files current so VS Code completes and checks scripts and input files the way
/// the engine does.
/// </summary>
/// <remarks>
/// The files are <c>.luaurc</c> (Luau mode), <c>Types/oxygen.d.luau</c> and the input file schemas copied from the
/// SDK, and <c>.vscode/settings.json</c>, which points <c>luau-lsp</c> at the definitions; <c>.luaurc</c> cannot name
/// definition files. Missing files are created and SDK copies refreshed when they differ. An existing VS Code settings
/// file only gains the keys it lacks; no value is ever overwritten, and a file with comments is left untouched.
/// </remarks>
/// <param name="paths">The SDK scripting files.</param>
/// <param name="operationResults">The publisher that records each change in Output.</param>
public sealed class ProjectScriptTooling(ScriptToolingPaths paths, IOperationResultPublisher operationResults)
{
    /// <summary>The project folder that holds the SDK copies.</summary>
    public const string TypesFolder = "Types";

    private static readonly JsonSerializerOptions Indented = new() { WriteIndented = true };

    /// <summary>Creates or refreshes the tooling files of a project and records the changes in Output.</summary>
    /// <param name="project">The project.</param>
    /// <returns>The changes made, one line each; empty when everything was current.</returns>
    public IReadOnlyList<string> Ensure(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        var root = project.ProjectRoot.FullPath;
        var changes = new List<string>();
        try
        {
            if (WriteIfMissing(Path.Combine(root, ".luaurc"), "{\n  \"languageMode\": \"nonstrict\"\n}\n"))
            {
                changes.Add("Created .luaurc");
            }

            CopyFromSdk(paths.DefinitionsPath, Path.Combine(root, TypesFolder, "oxygen.d.luau"), changes);
            CopyFromSdk(Path.Combine(paths.SchemaDirectory, "oxygen.input.schema.json"), Path.Combine(root, TypesFolder, "oxygen.input.schema.json"), changes);

            // The input schema references the shared name definition beside it.
            CopyFromSdk(Path.Combine(paths.SchemaDirectory, "oxygen.name.schema.json"), Path.Combine(root, TypesFolder, "oxygen.name.schema.json"), changes);
            this.MergeVsCodeSettings(Path.Combine(root, ".vscode", "settings.json"), changes);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            ScriptOperationResults.PublishFailure(
                operationResults,
                ScriptOperationKinds.UpdateTooling,
                ScriptDiagnosticCodes.ToolingUpdateFailed,
                "Script tooling not updated",
                $"Could not update the project's script tooling files: {exception.Message}. VS Code may not complete the Oxygen API.",
                root,
                exception);
            return changes;
        }

        if (changes.Count > 0)
        {
            ScriptOperationResults.PublishSuccess(operationResults, ScriptOperationKinds.UpdateTooling, "Script tooling updated", string.Join("; ", changes) + ".", root);
        }

        return changes;
    }

    /// <summary>Adds the keys VS Code needs to a settings object, keeping every existing value.</summary>
    /// <param name="settings">The settings object.</param>
    /// <returns>The keys that were added.</returns>
    internal static IReadOnlyList<string> AddMissingSettings(JsonObject settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        var added = new List<string>();
        AddIfMissing(settings, "luau-lsp.types.definitionFiles", () => new JsonArray($"{TypesFolder}/oxygen.d.luau"), added);
        AddIfMissing(settings, "luau-lsp.platform.type", () => "standard", added);
        AddIfMissing(settings, "luau-lsp.sourcemap.enabled", () => false, added);

        if (settings["files.associations"] is JsonObject associations)
        {
            AddIfMissing(associations, "*.lua", () => "luau", added, "files.associations *.lua");
        }
        else
        {
            AddIfMissing(settings, "files.associations", () => new JsonObject { ["*.lua"] = "luau" }, added);
        }

        var schemas = settings["json.schemas"] as JsonArray;
        if (schemas is null && !settings.ContainsKey("json.schemas"))
        {
            schemas = [];
            settings["json.schemas"] = schemas;
        }

        if (schemas is not null)
        {
            const string url = $"./{TypesFolder}/oxygen.input.schema.json";
            if (!schemas.OfType<JsonObject>().Any(entry => string.Equals(entry["url"]?.GetValue<string>(), url, StringComparison.OrdinalIgnoreCase)))
            {
                schemas.Add(new JsonObject { ["fileMatch"] = new JsonArray("*" + AssetFileTypes.InputFile), ["url"] = url });
                added.Add("json.schemas *" + AssetFileTypes.InputFile);
            }
        }

        return added;
    }

    private static void AddIfMissing(JsonObject target, string key, Func<JsonNode?> value, List<string> added, string? label = null)
    {
        if (!target.ContainsKey(key))
        {
            target[key] = value();
            added.Add(label ?? key);
        }
    }

    private static bool WriteIfMissing(string path, string text)
    {
        if (File.Exists(path))
        {
            return false;
        }

        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
        return true;
    }

    private static void CopyFromSdk(string source, string target, List<string> changes)
    {
        if (!File.Exists(source))
        {
            return;
        }

        var bytes = File.ReadAllBytes(source);
        var exists = File.Exists(target);
        if (exists && File.ReadAllBytes(target).AsSpan().SequenceEqual(bytes))
        {
            return;
        }

        _ = Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.WriteAllBytes(target, bytes);
        changes.Add((exists ? "Refreshed " : "Created ") + TypesFolder + "/" + Path.GetFileName(target) + " from the SDK");
    }

    private void MergeVsCodeSettings(string path, List<string> changes)
    {
        if (!File.Exists(path))
        {
            var created = new JsonObject();
            _ = AddMissingSettings(created);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, created.ToJsonString(Indented) + "\n");
            changes.Add("Created .vscode/settings.json for luau-lsp");
            return;
        }

        JsonObject? settings;
        try
        {
            // VS Code allows comments and trailing commas; such a file is never rewritten, since that would drop them.
            settings = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
        }
        catch (JsonException)
        {
            settings = null;
        }

        if (settings is null)
        {
            var missing = AddMissingSettings(new JsonObject());
            ScriptOperationResults.PublishFailure(
                operationResults,
                ScriptOperationKinds.UpdateTooling,
                ScriptDiagnosticCodes.ToolingUpdateFailed,
                "VS Code settings not updated",
                $".vscode/settings.json has comments or is not plain JSON, so it was left unchanged. For script completion, add: {string.Join(", ", missing)}.",
                path);
            return;
        }

        var added = AddMissingSettings(settings);
        if (added.Count > 0)
        {
            File.WriteAllText(path, settings.ToJsonString(Indented) + "\n");
            changes.Add("Added " + string.Join(", ", added) + " to .vscode/settings.json");
        }
    }
}
