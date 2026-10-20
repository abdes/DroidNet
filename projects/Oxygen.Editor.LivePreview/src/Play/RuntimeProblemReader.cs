// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Runtime.CompilerServices;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Interop.Scripting;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// Turns the script diagnostics a running player publishes into Problems entries, rendered with their source lines
/// in the engine's layout, like the analyzer's.
/// </summary>
/// <param name="project">The project whose files the diagnostics' spans name.</param>
/// <param name="fileSystem">The file system that holds the project.</param>
internal sealed class RuntimeProblemReader(ProjectContext project, IFileSystem fileSystem)
{
    /// <summary>Reads one diagnostic.</summary>
    /// <param name="diagnostic">The diagnostic, as <c>oxygen.script-diagnostic.schema.json</c> defines it.</param>
    /// <returns>The problem.</returns>
    public RuntimeScriptProblem Read(JsonObject diagnostic)
    {
        ArgumentNullException.ThrowIfNull(diagnostic);
        try
        {
            return this.ReadNative(diagnostic) ?? Unrendered(diagnostic);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DllNotFoundException or BadImageFormatException or TypeLoadException or FileLoadException)
        {
            return Unrendered(diagnostic);
        }
    }

    /// <summary>Finds the file a span's source name means.</summary>
    /// <param name="sourceName">A virtual path, or a path relative to an authoring mount or the project.</param>
    /// <returns>The existing file, or <see langword="null"/>.</returns>
    internal string? ResolveSource(string sourceName)
    {
        if (string.IsNullOrWhiteSpace(sourceName))
        {
            return null;
        }

        if (sourceName.StartsWith('/'))
        {
            return AssetPath.TryParse(sourceName, out var asset) && RelocationPaths.ToPhysical(project, asset) is { } physical && fileSystem.File.Exists(physical.FullPath) ? physical.FullPath : null;
        }

        return project.AuthoringMounts.Select(mount => fileSystem.Path.Combine(project.ProjectRoot.FullPath, mount.RelativePath, sourceName))
            .Append(fileSystem.Path.Combine(project.ProjectRoot.FullPath, sourceName))
            .Select(fileSystem.Path.GetFullPath)
            .FirstOrDefault(fileSystem.File.Exists);
    }

    // The analyzer renders the severity in lowercase, as in "error[S0503]: ...".
    private static string Label(DiagnosticSeverity severity) => severity switch
    {
        DiagnosticSeverity.Error or DiagnosticSeverity.Fatal => "error",
        DiagnosticSeverity.Warning => "warning",
        _ => "note",
    };

    // The schema requires a code and a message; a diagnostic without them still shows, under placeholders.
    private static RuntimeScriptProblem Unrendered(JsonObject diagnostic)
    {
        var code = diagnostic["code"]?.GetValue<string>() is { Length: > 0 } named ? named : "unknown";
        var message = diagnostic["message"]?.GetValue<string>() is { Length: > 0 } text ? text : "the script reported a problem";
        var severity = Severity(diagnostic["severity"]?.GetValue<string>());
        var slot = diagnostic["slot"] as JsonObject;
        return new RuntimeScriptProblem(severity, code, message, $"{Label(severity)}[{code}]: {message}\n")
        {
            SlotNode = slot?["node"]?.GetValue<string>(),
            SlotIndex = slot?["slot_index"]?.GetValue<int>(),
            SlotHook = slot?["hook"]?.GetValue<string>(),
        };
    }

    private static DiagnosticSeverity Severity(string? text)
        => text switch
        {
            "error" => DiagnosticSeverity.Error,
            "warning" => DiagnosticSeverity.Warning,
            _ => DiagnosticSeverity.Info,
        };

    // Loading the Interop assembly fails when the method that uses its types is compiled, so those types stay here
    // and the load failure surfaces at the call.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private RuntimeScriptProblem? ReadNative(JsonObject diagnostic)
    {
        if (ScriptAnalysis.Render(diagnostic.ToJsonString(), this.ReadSource) is not { } info)
        {
            return null;
        }

        return new RuntimeScriptProblem(
            info.Severity switch
            {
                ScriptDiagnosticSeverity.Error => DiagnosticSeverity.Error,
                ScriptDiagnosticSeverity.Warning => DiagnosticSeverity.Warning,
                _ => DiagnosticSeverity.Info,
            },
            info.Code,
            info.Message,
            info.Rendered)
        {
            File = info.PrimarySpan is { } span ? this.ResolveSource(span.SourceName) ?? span.SourceName : null,
            Line = info.PrimarySpan?.Line,
            Column = info.PrimarySpan?.Column,
            Explanation = info.Explanation,
            SlotNode = info.SlotNode,
            SlotIndex = info.SlotIndex,
            SlotHook = info.SlotHook,
        };
    }

    private string? ReadSource(string sourceName)
    {
        if (this.ResolveSource(sourceName) is not { } path)
        {
            return null;
        }

        try
        {
            return fileSystem.File.ReadAllText(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
