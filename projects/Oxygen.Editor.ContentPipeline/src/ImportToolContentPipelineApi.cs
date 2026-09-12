// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Bounded ImportTool fallback for ED-M07 engine content-pipeline operations.
/// </summary>
/// <param name="toolLocator">The native tool locator.</param>
/// <param name="processRunner">The contained worker runner.</param>
/// <param name="logger">The operation logger.</param>
/// <param name="artifactQualification">The fixed installed artifact verification service.</param>
public sealed partial class ImportToolContentPipelineApi(
    IEngineContentPipelineToolLocator toolLocator,
    IContentPipelineProcessRunner processRunner,
    ILogger<ImportToolContentPipelineApi> logger,
    IArtifactQualificationService? artifactQualification = null) : IEngineContentPipelineApi
{
    private static readonly JsonSerializerOptions ManifestJsonOptions = new() { WriteIndented = true };

    private readonly IEngineContentPipelineToolLocator toolLocator = toolLocator ?? throw new ArgumentNullException(nameof(toolLocator));
    private readonly IContentPipelineProcessRunner processRunner = processRunner ?? throw new ArgumentNullException(nameof(processRunner));
    private readonly ILogger<ImportToolContentPipelineApi> logger = logger ?? throw new ArgumentNullException(nameof(logger));
    private readonly IArtifactQualificationService artifactQualification = artifactQualification ?? EditorArtifactQualificationService.ForCurrentProcess();

    /// <inheritdoc />
    public async Task<NativeImportResult> ImportAsync(
        ContentImportExecution execution,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(execution);
        ArgumentNullException.ThrowIfNull(execution.Manifest, nameof(execution));
        cancellationToken.ThrowIfCancellationRequested();
        ValidateExecutionPaths(execution);

        var qualification = execution.Artifacts is { } borrowed
            ? new ArtifactQualificationResult(borrowed, [])
            : await this.artifactQualification.VerifyAsync(execution.OperationId, cancellationToken).ConfigureAwait(false);
        if (!qualification.Succeeded)
        {
            return new(Succeeded: false, qualification.Diagnostics);
        }

        var artifacts = qualification.Artifacts!;
        var manifest = execution.Manifest;
        var manifestPath = Path.Combine(execution.OperationRoot, "manifests", $"import-{Guid.NewGuid():N}.json");
        Task? retainedWorkerDrain = null;

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var toolPath = this.GetQualifiedToolPath(artifacts);
            Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);
            await WriteManifestAsync(manifest, manifestPath, cancellationToken).ConfigureAwait(false);
            var request = CreateImportRequest(toolPath, manifest.Output, manifestPath, execution.InputRoot) with
            {
                Output = CookRunContext.Current is { } progress ? new CookOutput(progress) : null,
            };

            this.LogImportToolInvoked(toolPath, manifestPath, execution.InputRoot);
            var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
            return result.ExitCode == 0
                ? new NativeImportResult(Succeeded: true, Diagnostics: [])
                : new NativeImportResult(Succeeded: false, Diagnostics: [CreateImportFailureDiagnostic(execution.OperationId, result)]);
        }
        catch (ContentPipelineTerminationException ex)
        {
            retainedWorkerDrain = ex.DrainCompletion;
            throw;
        }
        finally
        {
            if (retainedWorkerDrain is null)
            {
                if (execution.Artifacts is null)
                {
                    await artifacts.DisposeAsync().ConfigureAwait(false);
                }

                TryDeleteFile(manifestPath);
            }
            else
            {
                _ = ReleaseAfterWorkerDrainAsync(retainedWorkerDrain, manifestPath, execution.Artifacts is null ? artifacts : null);
            }
        }
    }

    /// <inheritdoc />
    public Task<CookInspectionResult> InspectLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(cookedRoot);
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        try
        {
            var document = ReadLooseCookedIndex(cookedRoot);
            return Task.FromResult(InspectDocument(operationId, cookedRoot, document));
        }
        catch (Exception ex) when (IsLooseCookedReadFailure(ex))
        {
            return Task.FromResult(new CookInspectionResult(
                cookedRoot,
                Succeeded: false,
                SourceIdentity: null,
                Assets: [],
                Files: [],
                Diagnostics:
                [
                    new DiagnosticRecord
                    {
                        OperationId = operationId,
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = ContentPipelineDiagnosticCodes.InspectFailed,
                        Message = "Cooked output inspection failed.",
                        TechnicalMessage = ex.Message,
                        ExceptionType = ex.GetType().FullName,
                        AffectedPath = cookedRoot,
                    },
                ]));
        }
    }

    /// <inheritdoc />
    public Task<CookValidationResult> ValidateLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(cookedRoot);
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        try
        {
            var document = ReadLooseCookedIndex(cookedRoot);
            var validationDiagnostics = ValidateLooseCookedDocument(
                operationId,
                cookedRoot,
                document,
                ContentPipelineDiagnosticCodes.ValidateFailed);
            return Task.FromResult(new CookValidationResult(cookedRoot, validationDiagnostics.Count == 0, validationDiagnostics));
        }
        catch (Exception ex) when (IsLooseCookedReadFailure(ex))
        {
            return Task.FromResult(new CookValidationResult(
                cookedRoot,
                Succeeded: false,
                Diagnostics:
                [
                    new DiagnosticRecord
                    {
                        OperationId = operationId,
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = ContentPipelineDiagnosticCodes.ValidateFailed,
                        Message = "Cooked output validation failed.",
                        TechnicalMessage = ex.Message,
                        ExceptionType = ex.GetType().FullName,
                        AffectedPath = cookedRoot,
                    },
                ]));
        }
    }

    private static CookInspectionResult InspectDocument(Guid operationId, string cookedRoot, Document document)
    {
        var validationDiagnostics = ValidateLooseCookedDocument(
            operationId,
            cookedRoot,
            document,
            ContentPipelineDiagnosticCodes.InspectFailed);
        return validationDiagnostics.Count > 0
            ? new CookInspectionResult(
                cookedRoot,
                Succeeded: false,
                SourceIdentity: document.SourceGuid,
                Assets: [],
                Files: [],
                validationDiagnostics)
            : new CookInspectionResult(
            cookedRoot,
            Succeeded: true,
            document.SourceGuid,
            document.Assets
                .OrderBy(static asset => asset.VirtualPath, StringComparer.Ordinal)
                .Select(static asset => new CookedAssetEntry(
                    asset.VirtualPath ?? asset.DescriptorRelativePath,
                    MapAssetKind(asset.AssetType)))
                .ToList(),
            document.Files
                .OrderBy(static file => file.RelativePath, StringComparer.Ordinal)
                .Select(static file => new CookedFileEntry(file.RelativePath, file.Size))
                .ToList(),
            Diagnostics: []);
    }

    private static async Task WriteManifestAsync(ContentImportManifest manifest, string path, CancellationToken cancellationToken)
    {
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(stream, manifest, ManifestJsonOptions, cancellationToken).ConfigureAwait(false);
        }
    }

    private static ContentPipelineProcessRequest CreateImportRequest(string toolPath, string output, string manifestPath, string inputRoot)
        => new(
            toolPath,
            ["--no-tui", "--no-color", "--cooked-root", output, "batch", "--manifest", manifestPath, "--root", inputRoot],
            inputRoot);

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Best-effort cleanup must not mask the primary content-pipeline result.")]
    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch (IOException)
        {
            // Best effort only.
        }
        catch (UnauthorizedAccessException)
        {
            // Best effort only.
        }
    }

    private static Document ReadLooseCookedIndex(string cookedRoot)
    {
        var indexPath = Path.Combine(cookedRoot, "container.index.bin");
        using var stream = File.OpenRead(indexPath);
        return LooseCookedIndex.Read(stream);
    }

    private static List<DiagnosticRecord> ValidateLooseCookedDocument(
        Guid operationId,
        string cookedRoot,
        Document document,
        string diagnosticCode)
    {
        var diagnostics = new List<DiagnosticRecord>();
        foreach (var asset in document.Assets)
        {
            if (string.IsNullOrWhiteSpace(asset.DescriptorRelativePath))
            {
                diagnostics.Add(CreateCookedValidationDiagnostic(
                    operationId,
                    diagnosticCode,
                    "Cooked asset descriptor path is missing.",
                    cookedRoot,
                    asset.VirtualPath));
                continue;
            }

            ValidateCookedFile(
                diagnostics,
                operationId,
                diagnosticCode,
                cookedRoot,
                asset.DescriptorRelativePath,
                asset.DescriptorSize,
                asset.VirtualPath,
                "asset descriptor");
        }

        foreach (var file in document.Files)
        {
            if (string.IsNullOrWhiteSpace(file.RelativePath))
            {
                diagnostics.Add(CreateCookedValidationDiagnostic(
                    operationId,
                    diagnosticCode,
                    "Cooked file record path is missing.",
                    cookedRoot,
                    affectedVirtualPath: null));
                continue;
            }

            ValidateCookedFile(
                diagnostics,
                operationId,
                diagnosticCode,
                cookedRoot,
                file.RelativePath,
                file.Size,
                affectedVirtualPath: null,
                "file record");
        }

        return diagnostics;
    }

    private static void ValidateCookedFile(
        List<DiagnosticRecord> diagnostics,
        Guid operationId,
        string diagnosticCode,
        string cookedRoot,
        string relativePath,
        ulong expectedSize,
        string? affectedVirtualPath,
        string kind)
    {
        var path = Path.Combine(cookedRoot, relativePath.Replace('/', Path.DirectorySeparatorChar));
        if (!File.Exists(path))
        {
            diagnostics.Add(CreateCookedValidationDiagnostic(
                operationId,
                diagnosticCode,
                $"Cooked {kind} is missing: {relativePath}.",
                path,
                affectedVirtualPath));
            return;
        }

        var actualSize = new FileInfo(path).Length;
        if (actualSize != (long)expectedSize)
        {
            diagnostics.Add(CreateCookedValidationDiagnostic(
                operationId,
                diagnosticCode,
                $"Cooked {kind} size mismatch for {relativePath}.",
                path,
                affectedVirtualPath,
                string.Create(CultureInfo.InvariantCulture, $"Expected {expectedSize} bytes, found {actualSize} bytes.")));
        }
    }

    private static DiagnosticRecord CreateCookedValidationDiagnostic(
        Guid operationId,
        string code,
        string message,
        string affectedPath,
        string? affectedVirtualPath,
        string? technicalMessage = null)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.ContentPipeline,
            Severity = DiagnosticSeverity.Error,
            Code = code,
            Message = message,
            TechnicalMessage = technicalMessage,
            AffectedPath = affectedPath,
            AffectedVirtualPath = affectedVirtualPath,
        };

    private static DiagnosticRecord CreateImportFailureDiagnostic(
        Guid operationId,
        ContentPipelineProcessResult result)
    {
        var technical = string.Join(
            Environment.NewLine,
            new[] { result.StandardError, result.StandardOutput }.Where(static text => !string.IsNullOrWhiteSpace(text)));

        return new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed,
            Message = "Native content import failed.",
            TechnicalMessage = string.IsNullOrWhiteSpace(technical) ? string.Create(CultureInfo.InvariantCulture, $"Exit code {result.ExitCode}.") : technical,
        };
    }

    private static void ValidateExecutionPaths(ContentImportExecution execution)
    {
        if (execution.OperationId == Guid.Empty
            || !Path.IsPathFullyQualified(execution.InputRoot)
            || !Path.IsPathFullyQualified(execution.OperationRoot)
            || !Path.IsPathFullyQualified(execution.Manifest.Output))
        {
            throw new ArgumentException("Native import requires an operation identity and absolute input, operation, and output paths.", nameof(execution));
        }
    }

    private static ContentCookAssetKind MapAssetKind(byte assetType)
        => assetType switch
        {
            1 => ContentCookAssetKind.Material,
            2 => ContentCookAssetKind.Geometry,
            3 => ContentCookAssetKind.Scene,
            _ => ContentCookAssetKind.Unknown,
        };

    private static bool IsLooseCookedReadFailure(Exception ex)
        => ex is IOException
            or UnauthorizedAccessException
            or InvalidDataException
            or NotSupportedException
            or InvalidOperationException
            or ArgumentException;

    [LoggerMessage(
        EventId = 1001,
        Level = LogLevel.Information,
        Message = "Invoking ImportTool '{ToolPath}' with manifest '{ManifestPath}' in '{WorkingDirectory}'.")]
    private partial void LogImportToolInvoked(string toolPath, string manifestPath, string workingDirectory);

    private sealed class CookOutput(IProgress<CookRunProgress> progress) : IProgress<ContentPipelineProcessOutput>
    {
        public void Report(ContentPipelineProcessOutput value) => progress.Report(new(Message: value.Text));
    }
}
