// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Analyzes a model source outside any cook: the import dialog's view of a file before it is imported.</summary>
/// <param name="native">The ImportTool, which runs <c>analyze-sources</c>.</param>
/// <param name="manifestBuilder">Builds the model's job, as its import will.</param>
public sealed class ModelSourceAnalyzer(IEngineContentPipelineApi native, IContentImportManifestBuilder manifestBuilder) : IModelSourceAnalyzer
{
    /// <inheritdoc />
    public async Task<ModelSourceAnalysis> AnalyzeAsync(ProjectContext project, string sourcePath, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentException.ThrowIfNullOrEmpty(sourcePath);
        var operationId = Guid.NewGuid();
        var source = Path.GetFullPath(sourcePath);
        if (!File.Exists(source))
        {
            return Failed(operationId, source, AssetImportDiagnosticCodes.SourceMissing, $"'{Path.GetFileName(source)}' is missing.", technical: null);
        }

        // The analysis names no destination: any identity under an authoring mount gives the job its products' names.
        var file = Path.GetFileName(source);
        var name = Path.GetFileNameWithoutExtension(source);
        var mount = project.SourceMounts.FirstOrDefault()?.Name ?? AssetPath.Content.Mount;
        var job = manifestBuilder.BuildModelJob(file, [], name, AssetPath.Create(mount, name + "/" + file));
        var scratch = ProjectOutputFolders.BuildPath(project.ProjectRoot.FullPath, "import-analysis", operationId.ToString("N"));
        try
        {
            // The source's folder is the analysis root and its one mount: a manifest names no absolute path, and no
            // .mounts.json file above the folder applies.
            var execution = new ContentSourceAnalysisExecution(operationId, Path.GetDirectoryName(source)!, scratch, [job])
            {
                Mounts = [new ContentImportMount("Source", ".")],
                MountsFile = ContentImportMountsFile.Empty,
            };
            var report = await native.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
            var facts = report.Jobs.Single();
            return new(report.Complete && facts.Complete, facts.Model, facts.Diagnostics);
        }
        catch (NativeCompatibilityException failure)
        {
            return new(Complete: false, Facts: null, failure.Diagnostics);
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or System.ComponentModel.Win32Exception)
        {
            return Failed(operationId, source, AssetImportDiagnosticCodes.ImportFailed, $"'{file}' could not be analyzed.", failure.Message);
        }
        finally
        {
            TryDelete(scratch);
        }
    }

    private static ModelSourceAnalysis Failed(Guid operationId, string source, string code, string message, string? technical)
        => new(Complete: false, Facts: null, [new()
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = code,
            Message = message,
            TechnicalMessage = technical,
            AffectedPath = source,
        },
        ]);

    private static void TryDelete(string folder)
    {
        try
        {
            if (Directory.Exists(folder))
            {
                Directory.Delete(folder, recursive: true);
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            // The project's build folder is scratch; a leftover analysis folder does no harm.
        }
    }
}
