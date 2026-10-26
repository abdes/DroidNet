// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ModelEditor.TestSupport;

/// <summary>Builds import dialogs over a project on a mock file system, with an analyzer that answers at once.</summary>
internal static class ImportDialogs
{
    /// <summary>The folder the dialogs import into.</summary>
    public static readonly AssetPath Destination = AssetPath.Parse("/Content/Vehicles");

    /// <summary>Creates a project with a source file outside it for each name.</summary>
    /// <param name="files">The source files' names, created in a folder outside the project.</param>
    /// <returns>The project and the full paths of its sources.</returns>
    public static (ProjectContext Project, string[] Sources) CreateProject(params string[] files)
    {
        var fileSystem = new MockFileSystem();
        var project = TestProject.Create(fileSystem, @"C:\Projects\Garage").Context;
        _ = fileSystem.Directory.CreateDirectory(@"D:\Sources");
        var sources = files.Select(file => Path.Combine(@"D:\Sources", file)).ToArray();
        foreach (var source in sources)
        {
            _ = fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(source)!);
            fileSystem.File.WriteAllText(source, "model");
        }

        return (project, sources);
    }

    /// <summary>Creates a dialog whose analyzer reports <paramref name="facts"/> for every file.</summary>
    /// <param name="project">The project.</param>
    /// <param name="facts">The facts of each file, by file name; a file without facts is analyzed as incomplete.</param>
    /// <param name="dialogs">The pickers; none when null.</param>
    /// <param name="findings">The diagnostics the analysis of each file reports, by file name; none when absent.</param>
    /// <returns>The dialog's view model, which the caller disposes.</returns>
    public static ModelImportDialogViewModel Create(
        ProjectContext project,
        IReadOnlyDictionary<string, ModelSourceFacts>? facts = null,
        IDialogService? dialogs = null,
        IReadOnlyDictionary<string, ImmutableArray<DiagnosticRecord>>? findings = null)
    {
        var analyzer = new Mock<IModelSourceAnalyzer>();
        _ = analyzer.Setup(value => value.AnalyzeAsync(It.IsAny<ProjectContext>(), It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .Returns<ProjectContext, string, CancellationToken>((_, path, _) =>
            {
                var name = Path.GetFileName(path);
                var reported = findings is not null && findings.TryGetValue(name, out var listed) ? listed : [];
                return Task.FromResult(
                    facts?.GetValueOrDefault(name) is { } known
                        ? new ModelSourceAnalysis(Complete: true, known, reported)
                        : new ModelSourceAnalysis(Complete: false, Facts: null, reported));
            });
        return new(project, dialogs ?? Mock.Of<IDialogService>(), analyzer.Object, Destination, Defaults());
    }

    /// <summary>Gets the facts of a file of <paramref name="format"/>.</summary>
    /// <param name="format">The file's format: <c>gltf</c>, <c>glb</c> or <c>fbx</c>.</param>
    /// <param name="materials">The materials it produces.</param>
    /// <returns>The facts.</returns>
    public static ModelSourceFacts Facts(string format, params string[] materials)
        => new()
        {
            Format = format,
            FormatVersion = format == "fbx" ? "7.4" : "2.0",
            UnitMeters = format == "fbx" ? 0.01 : 1,
            Right = "+X",
            Up = format == "fbx" ? "+Z" : "+Y",
            Front = "-Y",
            IsLeftHanded = false,
            NodeCount = 14,
            MeshCount = 9,
            Materials = [.. materials],
        };

    /// <summary>Gets a diagnostic the analysis of a file reports.</summary>
    /// <param name="severity">Its severity.</param>
    /// <param name="message">Its message.</param>
    /// <returns>The diagnostic.</returns>
    public static DiagnosticRecord Finding(DiagnosticSeverity severity, string message)
        => new()
        {
            OperationId = Guid.Empty,
            Domain = FailureDomain.AssetImport,
            Severity = severity,
            Code = "import.source_feature_dropped",
            Message = message,
        };

    /// <summary>Gets the ImportTool's defaults, as a schema would state them.</summary>
    /// <returns>Run defaults.</returns>
    public static ImportRunSettings Defaults()
        => new()
        {
            ThreadPoolSize = 16,
            MaxInFlightJobs = 16,
            Concurrency = ImportRunSettings.Pipelines.ToImmutableDictionary(static name => name, static _ => new ImportPipelineSettings(2, 64), StringComparer.Ordinal),
        };

    /// <summary>Gets the value a file's settings hold for a key.</summary>
    /// <param name="file">A file of the dialog.</param>
    /// <param name="key">The key.</param>
    /// <returns>The value, or null when unset.</returns>
    public static JsonElement? Setting(ModelImportFile file, string key)
        => file.Settings.TryGetProperty(key, out var value) ? value : null;
}
