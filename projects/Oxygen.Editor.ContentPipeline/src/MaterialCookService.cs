// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Default editor material cook service for the ED-M05 scalar material slice.
/// </summary>
/// <param name="importService">The asset import service.</param>
/// <param name="cookCoordinator">The shared project writer and lifetime coordinator.</param>
/// <param name="cookDocuments">The registered saved-document owners.</param>
/// <param name="logger">The logger.</param>
/// <param name="projectContextService">Optional active project context service used for state queries.</param>
public sealed partial class MaterialCookService(
    IImportService importService,
    IContentCookCoordinator cookCoordinator,
    ICookDocumentRegistry cookDocuments,
    ILogger<MaterialCookService> logger,
    IProjectContextService? projectContextService = null) : IMaterialCookService
{
    private readonly IImportService importService = importService ?? throw new ArgumentNullException(nameof(importService));
    private readonly IContentCookCoordinator cookCoordinator = cookCoordinator ?? throw new ArgumentNullException(nameof(cookCoordinator));
    private readonly ILogger<MaterialCookService> logger = logger ?? throw new ArgumentNullException(nameof(logger));
    private readonly IProjectContextService? projectContextService = projectContextService;

    /// <inheritdoc />
    public async Task<MaterialCookResult> CookMaterialAsync(
        MaterialCookRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        if (string.IsNullOrWhiteSpace(request.ProjectRoot)
            || string.IsNullOrWhiteSpace(request.MountName)
            || string.IsNullOrWhiteSpace(request.SourceRelativePath))
        {
            this.LogMaterialCookRejected(request.MaterialSourceUri, request.ProjectRoot, request.MountName, request.SourceRelativePath);
            return new MaterialCookResult(
                request.MaterialSourceUri,
                CookedMaterialUri: null,
                MaterialCookState.Rejected,
                OperationId: null);
        }

        return await this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, request.MaterialSourceUri),
            async (operation, token) =>
            {
                CookRunContext.Report(new(Asset: new(request.MaterialSourceUri, ContentCookAssetKind.Material, CookAssetState.Preparing)));
                if (!string.Equals(
                    Path.TrimEndingDirectorySeparator(Path.GetFullPath(request.ProjectRoot)),
                    Path.TrimEndingDirectorySeparator(Path.GetFullPath(operation.Project.ProjectRoot)),
                    StringComparison.OrdinalIgnoreCase))
                {
                    return new MaterialCookResult(request.MaterialSourceUri, CookedMaterialUri: null, MaterialCookState.Rejected, operation.OperationId);
                }

                using (var reads = await cookDocuments.AcquireAsync([Path.GetFullPath(Path.Combine(request.ProjectRoot, request.SourceRelativePath))], token).ConfigureAwait(false))
                {
                    var dirty = reads.Documents.Where(static document => document.IsDirty).ToArray();
                    if (dirty.Length != 0)
                    {
                        throw new CookInputsNeedSaveException(dirty);
                    }
                }

                var result = await this.CookMaterialCoreAsync(request, token).ConfigureAwait(false);
                return result with { OperationId = operation.OperationId };
            },
            cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public Task<MaterialCookState> GetMaterialCookStateAsync(
        Uri materialSourceUri,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(materialSourceUri);
        cancellationToken.ThrowIfCancellationRequested();

        if (this.projectContextService?.ActiveProject is not { } project
            || !TryResolveSourcePath(project, materialSourceUri, out var sourcePath, out _))
        {
            return Task.FromResult(MaterialCookState.NotCooked);
        }

        var cookedUri = GetCookedUri(materialSourceUri);
        if (!CookedOutputIsVisible(project.ProjectRoot, cookedUri))
        {
            return Task.FromResult(MaterialCookState.NotCooked);
        }

        var cookedPath = GetCookedPath(project.ProjectRoot, cookedUri);
        if (!File.Exists(sourcePath))
        {
            return Task.FromResult(MaterialCookState.Cooked);
        }

        var sourceTime = File.GetLastWriteTimeUtc(sourcePath);
        var cookedTime = File.GetLastWriteTimeUtc(cookedPath);
        return Task.FromResult(sourceTime > cookedTime ? MaterialCookState.Stale : MaterialCookState.Cooked);
    }

    private static string NormalizeRelativePath(string sourceRelativePath)
        => sourceRelativePath.Replace('\\', '/').TrimStart('/');

    private static string SummarizeDiagnostics(IReadOnlyList<ImportDiagnostic> diagnostics)
        => diagnostics.Count == 0 ? "<none>" : string.Join(
            "; ",
            diagnostics.Select(static diagnostic =>
                $"{diagnostic.Severity}:{diagnostic.Code} source='{diagnostic.SourcePath ?? string.Empty}' virtual='{diagnostic.VirtualPath ?? string.Empty}' message='{diagnostic.Message}'"));

    private static MaterialCookResult Failed(MaterialCookRequest request)
        => new(
            request.MaterialSourceUri,
            CookedMaterialUri: null,
            MaterialCookState.Failed,
            OperationId: null);

    private static Uri ToAssetUri(string virtualPath)
    {
        var path = virtualPath.StartsWith('/') ? virtualPath : "/" + virtualPath;
        return new Uri($"{AssetUris.Scheme}://{path}");
    }

    private static bool CookedOutputIsVisible(string projectRoot, Uri cookedUri)
    {
        var relative = GetCookedRelativePath(cookedUri).Replace('/', Path.DirectorySeparatorChar);
        var mount = GetMountPoint(cookedUri);
        var cookedOutput = GetCookedPath(projectRoot, cookedUri);
        var index = Path.Combine(projectRoot, ".cooked", mount, "container.index.bin");
        if (!File.Exists(cookedOutput) || !File.Exists(index))
        {
            return false;
        }

        using var stream = File.OpenRead(index);
        var document = LooseCookedIndex.Read(stream);
        var expectedVirtualPath = "/" + GetCookedRelativePath(cookedUri);
        var asset = document.Assets.FirstOrDefault(asset => string.Equals(asset.VirtualPath, expectedVirtualPath, StringComparison.Ordinal));
        if (asset is null)
        {
            return false;
        }

        var actualSize = new FileInfo(cookedOutput).Length;
        return actualSize >= 0 && (ulong)actualSize == asset.DescriptorSize;
    }

    private static string GetCookedPath(string projectRoot, Uri cookedUri)
        => Path.Combine(projectRoot, ".cooked", GetCookedRelativePath(cookedUri).Replace('/', Path.DirectorySeparatorChar));

    private static string GetCookedRelativePath(Uri cookedUri)
        => cookedUri.AbsolutePath.TrimStart('/').Replace('\\', '/');

    private static string GetMountPoint(Uri assetUri)
    {
        var relative = GetCookedRelativePath(assetUri);
        var slash = relative.IndexOf('/', StringComparison.Ordinal);
        return slash <= 0 ? string.Empty : relative[..slash];
    }

    private static Uri GetCookedUri(Uri materialSourceUri)
    {
        var path = materialSourceUri.AbsolutePath;
        if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
        {
            path = path[..^".json".Length];
        }

        return new Uri($"{AssetUris.Scheme}://{path}");
    }

    private static bool TryResolveSourcePath(
        ProjectContext project,
        Uri materialSourceUri,
        out string sourcePath,
        out string sourceRelativePath)
    {
        sourcePath = string.Empty;
        sourceRelativePath = string.Empty;
        if (!string.Equals(materialSourceUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var path = Uri.UnescapeDataString(materialSourceUri.AbsolutePath).TrimStart('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return false;
        }

        var mountName = path[..slash];
        var mountRelativePath = path[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return false;
        }

        sourceRelativePath = Path.Combine(mount.RelativePath, mountRelativePath).Replace('\\', '/');
        sourcePath = Path.GetFullPath(Path.Combine(project.ProjectRoot, sourceRelativePath));
        return true;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The editor cook boundary reports importer failures; cancellation and retained native worker ownership are rethrown above.")]
    private async Task<MaterialCookResult> CookMaterialCoreAsync(MaterialCookRequest request, CancellationToken cancellationToken)
    {
        var cookedUri = GetCookedUri(request.MaterialSourceUri);
        var virtualPath = "/" + GetCookedRelativePath(cookedUri);
        this.LogMaterialCookStarted(request.MaterialSourceUri, request.ProjectRoot, request.MountName, request.SourceRelativePath, virtualPath);

        try
        {
            CookRunContext.Report(new(Message: "Cooking material.", State: CookRunState.Cooking, Asset: new(request.MaterialSourceUri, ContentCookAssetKind.Material, CookAssetState.Cooking)));
            var result = await this.ImportMaterialAsync(request, virtualPath, cancellationToken).ConfigureAwait(false);
            return this.CreateCookResult(request, result);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (ContentPipelineTerminationException)
        {
            throw;
        }
        catch (Exception ex)
        {
            CookRunContext.Report(new(Message: ex.Message, Severity: Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error));
            this.LogMaterialCookException(request.MaterialSourceUri, ex);
            return new MaterialCookResult(
                request.MaterialSourceUri,
                CookedMaterialUri: null,
                MaterialCookState.Failed,
                OperationId: null);
        }
    }

    private async Task<ImportResult> ImportMaterialAsync(
        MaterialCookRequest request,
        string virtualPath,
        CancellationToken cancellationToken)
    {
        var importRequest = new ImportRequest(
            ProjectRoot: request.ProjectRoot,
            Inputs:
            [
                new ImportInput(
                    SourcePath: NormalizeRelativePath(request.SourceRelativePath),
                    MountPoint: request.MountName,
                    VirtualPath: virtualPath),
            ],
            Options: new ImportOptions(ReimportIfUnchanged: true, FailFast: request.FailFast));

        return await this.importService.ImportAsync(importRequest, cancellationToken).ConfigureAwait(false);
    }

    private MaterialCookResult CreateCookResult(MaterialCookRequest request, ImportResult result)
    {
        foreach (var diagnostic in result.Diagnostics)
        {
            var severity = diagnostic.Severity switch
            {
                ImportDiagnosticSeverity.Error => Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error,
                ImportDiagnosticSeverity.Warning => Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Warning,
                _ => Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Info,
            };
            CookRunContext.Report(new(Message: diagnostic.Message, Severity: severity));
        }

        if (!result.Succeeded)
        {
            this.LogMaterialCookFailed(request.MaterialSourceUri, result.Diagnostics.Count, SummarizeDiagnostics(result.Diagnostics));
            this.LogMaterialCookDiagnostics(result.Diagnostics);
            return Failed(request);
        }

        var cookedUri = result.Imported
            .Where(static asset => string.Equals(asset.AssetType, "Material", StringComparison.OrdinalIgnoreCase))
            .Select(static asset => ToAssetUri(asset.VirtualPath))
            .FirstOrDefault();
        if (cookedUri is not null && !CookedOutputIsVisible(request.ProjectRoot, cookedUri))
        {
            this.LogMaterialCookOutputMissing(request.MaterialSourceUri, cookedUri, request.ProjectRoot);
            this.LogMaterialCookDiagnostics(result.Diagnostics);
            return Failed(request);
        }

        this.LogMaterialCookSucceeded(request.MaterialSourceUri, cookedUri, result.Imported.Count, result.Diagnostics.Count);
        CookRunContext.Report(new(Asset: new(request.MaterialSourceUri, ContentCookAssetKind.Material, CookAssetState.Updated)));
        return new MaterialCookResult(
            request.MaterialSourceUri,
            cookedUri,
            cookedUri is null ? MaterialCookState.NotCooked : MaterialCookState.Cooked,
            OperationId: null);
    }

    private void LogMaterialCookDiagnostics(IReadOnlyList<ImportDiagnostic> diagnostics)
    {
        foreach (var diagnostic in diagnostics)
        {
            this.LogMaterialCookDiagnostic(
                diagnostic.Severity.ToString(),
                diagnostic.Code,
                diagnostic.Message,
                diagnostic.SourcePath ?? string.Empty,
                diagnostic.VirtualPath ?? string.Empty);
        }
    }

    [LoggerMessage(
        EventId = 1000,
        Level = LogLevel.Information,
        Message = "Material cook started for {MaterialUri}. ProjectRoot='{ProjectRoot}', Mount='{MountName}', Source='{SourceRelativePath}', VirtualPath='{VirtualPath}'.")]
    private partial void LogMaterialCookStarted(Uri materialUri, string projectRoot, string mountName, string sourceRelativePath, string virtualPath);

    [LoggerMessage(
        EventId = 1001,
        Level = LogLevel.Warning,
        Message = "Material cook failed for {MaterialUri} with {DiagnosticCount} diagnostics: {DiagnosticSummary}")]
    private partial void LogMaterialCookFailed(Uri materialUri, int diagnosticCount, string diagnosticSummary);

    [LoggerMessage(
        EventId = 1002,
        Level = LogLevel.Warning,
        Message = "Material cook failed for {MaterialUri}.")]
    private partial void LogMaterialCookException(Uri materialUri, Exception exception);

    [LoggerMessage(
        EventId = 1003,
        Level = LogLevel.Warning,
        Message = "Material cook rejected for {MaterialUri}; missing project facts. ProjectRoot='{ProjectRoot}', Mount='{MountName}', Source='{SourceRelativePath}'.")]
    private partial void LogMaterialCookRejected(Uri materialUri, string projectRoot, string mountName, string sourceRelativePath);

    [LoggerMessage(
        EventId = 1004,
        Level = LogLevel.Warning,
        Message = "Material cook output was imported but is not visible in loose cooked index. MaterialUri={MaterialUri}, CookedUri={CookedUri}, ProjectRoot='{ProjectRoot}'.")]
    private partial void LogMaterialCookOutputMissing(Uri materialUri, Uri cookedUri, string projectRoot);

    [LoggerMessage(
        EventId = 1005,
        Level = LogLevel.Information,
        Message = "Material cook succeeded for {MaterialUri}. CookedUri={CookedUri}, ImportedAssets={ImportedAssetCount}, Diagnostics={DiagnosticCount}.")]
    private partial void LogMaterialCookSucceeded(Uri materialUri, Uri? cookedUri, int importedAssetCount, int diagnosticCount);

    [LoggerMessage(
        EventId = 1006,
        Level = LogLevel.Warning,
        Message = "Material cook diagnostic: Severity={Severity}, Code={Code}, Source='{SourcePath}', VirtualPath='{VirtualPath}', Message='{Message}'.")]
    private partial void LogMaterialCookDiagnostic(string severity, string code, string message, string sourcePath, string virtualPath);
}
