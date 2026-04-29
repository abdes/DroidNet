// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Bounded ImportTool fallback for ED-M07 engine content-pipeline operations.
/// </summary>
public sealed partial class ImportToolContentPipelineApi : IEngineContentPipelineApi
{
    private static readonly JsonSerializerOptions ManifestJsonOptions = new() { WriteIndented = true };

    private readonly IEngineContentPipelineToolLocator toolLocator;
    private readonly IContentPipelineProcessRunner processRunner;
    private readonly ILogger<ImportToolContentPipelineApi> logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="ImportToolContentPipelineApi"/> class.
    /// </summary>
    /// <param name="toolLocator">The native tool locator.</param>
    /// <param name="processRunner">The process runner.</param>
    /// <param name="logger">The logger.</param>
    public ImportToolContentPipelineApi(
        IEngineContentPipelineToolLocator toolLocator,
        IContentPipelineProcessRunner processRunner,
        ILogger<ImportToolContentPipelineApi> logger)
    {
        this.toolLocator = toolLocator ?? throw new ArgumentNullException(nameof(toolLocator));
        this.processRunner = processRunner ?? throw new ArgumentNullException(nameof(processRunner));
        this.logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    /// <inheritdoc />
    public async Task<NativeImportResult> ImportAsync(
        ContentImportManifest manifest,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        var toolPath = this.toolLocator.GetImportToolPath();
        var projectRoot = InferProjectRoot(manifest.Output);
        var manifestPath = Path.Combine(projectRoot, ".pipeline", "Manifests", $"import-{operationId:N}.json");
        Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);

        try
        {
            using (var stream = File.Create(manifestPath))
            {
                await JsonSerializer.SerializeAsync(stream, manifest, ManifestJsonOptions, cancellationToken)
                    .ConfigureAwait(false);
            }

            var request = new ContentPipelineProcessRequest(
                toolPath,
                [
                    "--no-tui",
                    "--no-color",
                    "--quiet",
                    "--cooked-root",
                    manifest.Output,
                    "batch",
                    "--manifest",
                    manifestPath,
                    "--root",
                    projectRoot,
                ],
                projectRoot);

            this.LogImportToolInvoked(toolPath, manifestPath, projectRoot);
            var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
            if (result.ExitCode == 0)
            {
                return new NativeImportResult(Succeeded: true, Diagnostics: []);
            }

            return new NativeImportResult(
                Succeeded: false,
                Diagnostics: [CreateImportFailureDiagnostic(operationId, result)]);
        }
        finally
        {
            TryDeleteFile(manifestPath);
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
            var validationDiagnostics = ValidateLooseCookedDocument(
                operationId,
                cookedRoot,
                document,
                ContentPipelineDiagnosticCodes.InspectFailed);
            if (validationDiagnostics.Count > 0)
            {
                return Task.FromResult(new CookInspectionResult(
                    cookedRoot,
                    Succeeded: false,
                    SourceIdentity: document.SourceGuid,
                    Assets: [],
                    Files: [],
                    validationDiagnostics));
            }

            return Task.FromResult(new CookInspectionResult(
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
                Diagnostics: []));
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
            if (validationDiagnostics.Count > 0)
            {
                return Task.FromResult(new CookValidationResult(cookedRoot, Succeeded: false, validationDiagnostics));
            }

            return Task.FromResult(new CookValidationResult(cookedRoot, Succeeded: true, Diagnostics: []));
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

    private static IReadOnlyList<DiagnosticRecord> ValidateLooseCookedDocument(
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

            var descriptorPath = Path.Combine(
                cookedRoot,
                asset.DescriptorRelativePath.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(descriptorPath))
            {
                diagnostics.Add(CreateCookedValidationDiagnostic(
                    operationId,
                    diagnosticCode,
                    $"Cooked asset descriptor is missing: {asset.DescriptorRelativePath}.",
                    descriptorPath,
                    asset.VirtualPath));
                continue;
            }

            var actualSize = new FileInfo(descriptorPath).Length;
            if (actualSize != (long)asset.DescriptorSize)
            {
                diagnostics.Add(CreateCookedValidationDiagnostic(
                    operationId,
                    diagnosticCode,
                    $"Cooked asset descriptor size mismatch for {asset.DescriptorRelativePath}.",
                    descriptorPath,
                    asset.VirtualPath,
                    $"Expected {asset.DescriptorSize} bytes, found {actualSize} bytes."));
            }
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

            var filePath = Path.Combine(cookedRoot, file.RelativePath.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(filePath))
            {
                diagnostics.Add(CreateCookedValidationDiagnostic(
                    operationId,
                    diagnosticCode,
                    $"Cooked file record is missing: {file.RelativePath}.",
                    filePath,
                    affectedVirtualPath: null));
                continue;
            }

            var actualSize = new FileInfo(filePath).Length;
            if (actualSize != (long)file.Size)
            {
                diagnostics.Add(CreateCookedValidationDiagnostic(
                    operationId,
                    diagnosticCode,
                    $"Cooked file record size mismatch for {file.RelativePath}.",
                    filePath,
                    affectedVirtualPath: null,
                    $"Expected {file.Size} bytes, found {actualSize} bytes."));
            }
        }

        return diagnostics;
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
            TechnicalMessage = string.IsNullOrWhiteSpace(technical) ? $"Exit code {result.ExitCode}." : technical,
        };
    }

    private static string InferProjectRoot(string cookedMountRoot)
    {
        var mountDirectory = new DirectoryInfo(cookedMountRoot);
        var cookedDirectory = mountDirectory.Parent;
        if (cookedDirectory is not null
            && string.Equals(cookedDirectory.Name, ".cooked", StringComparison.OrdinalIgnoreCase)
            && cookedDirectory.Parent is { } projectRoot)
        {
            return projectRoot.FullName;
        }

        throw new InvalidOperationException(
            $"Cooked mount root '{cookedMountRoot}' must be under '<ProjectRoot>\\.cooked\\<MountName>'.");
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
}
