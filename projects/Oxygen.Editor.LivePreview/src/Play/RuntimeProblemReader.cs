// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Interop.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// Turns the script diagnostics a running player publishes into Problems entries, rendered with their source lines
/// in the engine's layout, like the analyzer's.
/// </summary>
/// <param name="project">The project whose files the diagnostics' spans name.</param>
internal sealed class RuntimeProblemReader(ProjectContext project)
{
    /// <summary>Reads one diagnostic.</summary>
    /// <param name="diagnostic">The diagnostic, as <c>oxygen.script-diagnostic.schema.json</c> defines it.</param>
    /// <returns>The problem.</returns>
    public ScriptProblem Read(JsonObject diagnostic)
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
            return AssetPath.TryParse(sourceName, out var asset) && RelocationPaths.ToPhysical(project, asset) is { } physical && File.Exists(physical.FullPath) ? physical.FullPath : null;
        }

        return project.AuthoringMounts.Select(mount => Path.Combine(project.ProjectRoot.FullPath, mount.RelativePath, sourceName))
            .Append(Path.Combine(project.ProjectRoot.FullPath, sourceName))
            .Select(Path.GetFullPath)
            .FirstOrDefault(File.Exists);
    }

    // The analyzer renders the severity in lowercase, as in "error[S0503]: ...".
    private static string Label(ScriptProblemSeverity severity) => severity switch
    {
        ScriptProblemSeverity.Error => "error",
        ScriptProblemSeverity.Warning => "warning",
        _ => "note",
    };

    private static ScriptProblem Unrendered(JsonObject diagnostic)
    {
        var code = diagnostic["code"]?.GetValue<string>() ?? string.Empty;
        var message = diagnostic["message"]?.GetValue<string>() ?? string.Empty;
        var severity = Severity(diagnostic["severity"]?.GetValue<string>());
        var slot = diagnostic["slot"] as JsonObject;
        return new ScriptProblem(severity, code, message, string.Empty, Line: null, Column: null, $"{Label(severity)}[{code}]: {message}\n", string.Empty)
        {
            SlotNode = slot?["node"]?.GetValue<string>(),
            SlotIndex = slot?["slot_index"]?.GetValue<int>(),
            IsRuntime = true,
        };
    }

    private static ScriptProblemSeverity Severity(string? text)
        => text switch
        {
            "error" => ScriptProblemSeverity.Error,
            "warning" => ScriptProblemSeverity.Warning,
            _ => ScriptProblemSeverity.Note,
        };

    // Loading the Interop assembly fails when the method that uses its types is compiled, so those types stay here
    // and the load failure surfaces at the call.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private ScriptProblem? ReadNative(JsonObject diagnostic)
    {
        if (ScriptAnalysis.Render(diagnostic.ToJsonString(), this.ReadSource) is not { } info)
        {
            return null;
        }

        var source = info.PrimarySpan is { } span ? this.ResolveSource(span.SourceName) ?? span.SourceName : string.Empty;
        return new ScriptProblem(
            info.Severity switch
            {
                ScriptDiagnosticSeverity.Error => ScriptProblemSeverity.Error,
                ScriptDiagnosticSeverity.Warning => ScriptProblemSeverity.Warning,
                _ => ScriptProblemSeverity.Note,
            },
            info.Code,
            info.Message,
            source,
            info.PrimarySpan?.Line,
            info.PrimarySpan?.Column,
            info.Rendered,
            info.Explanation)
        {
            SlotNode = info.SlotNode,
            SlotIndex = info.SlotIndex,
            IsRuntime = true,
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
            return File.ReadAllText(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
