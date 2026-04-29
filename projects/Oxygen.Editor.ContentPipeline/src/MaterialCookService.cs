// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Assets.Import;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Core;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Default editor material cook service for the ED-M05 scalar material slice.
/// </summary>
public sealed partial class MaterialCookService : IMaterialCookService
{
    private readonly IImportService importService;
    private readonly ILogger<MaterialCookService> logger;
    private readonly IProjectContextService? projectContextService;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialCookService"/> class.
    /// </summary>
    /// <param name="importService">The asset import service.</param>
    /// <param name="logger">The logger.</param>
    /// <param name="projectContextService">Optional active project context service used for state queries.</param>
    public MaterialCookService(
        IImportService importService,
        ILogger<MaterialCookService> logger,
        IProjectContextService? projectContextService = null)
    {
        this.importService = importService ?? throw new ArgumentNullException(nameof(importService));
        this.logger = logger ?? throw new ArgumentNullException(nameof(logger));
        this.projectContextService = projectContextService;
    }

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
            return new MaterialCookResult(
                request.MaterialSourceUri,
                CookedMaterialUri: null,
                MaterialCookState.Rejected,
                OperationId: null);
        }

        try
        {
            var result = await this.importService.ImportAsync(
                new ImportRequest(
                    ProjectRoot: request.ProjectRoot,
                    Inputs:
                    [
                        new ImportInput(
                            SourcePath: NormalizeRelativePath(request.SourceRelativePath),
                            MountPoint: request.MountName,
                            VirtualPath: "/" + GetCookedRelativePath(GetCookedUri(request.MaterialSourceUri))),
                    ],
                    Options: new ImportOptions(ReimportIfUnchanged: true, FailFast: request.FailFast)),
                cancellationToken).ConfigureAwait(false);

            if (!result.Succeeded)
            {
                this.LogMaterialCookFailed(request.MaterialSourceUri, result.Diagnostics.Count);
                return new MaterialCookResult(
                    request.MaterialSourceUri,
                    CookedMaterialUri: null,
                    MaterialCookState.Failed,
                    OperationId: null);
            }

            var cookedUri = result.Imported
                .Where(static asset => string.Equals(asset.AssetType, "Material", StringComparison.OrdinalIgnoreCase))
                .Select(static asset => ToAssetUri(asset.VirtualPath))
                .FirstOrDefault();
            if (cookedUri is not null && !CookedOutputIsVisible(request.ProjectRoot, cookedUri))
            {
                this.LogMaterialCookFailed(request.MaterialSourceUri, result.Diagnostics.Count);
                return new MaterialCookResult(
                    request.MaterialSourceUri,
                    CookedMaterialUri: null,
                    MaterialCookState.Failed,
                    OperationId: null);
            }

            return new MaterialCookResult(
                request.MaterialSourceUri,
                cookedUri,
                cookedUri is null ? MaterialCookState.NotCooked : MaterialCookState.Cooked,
                OperationId: null);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            this.LogMaterialCookException(request.MaterialSourceUri, ex);
            return new MaterialCookResult(
                request.MaterialSourceUri,
                CookedMaterialUri: null,
                MaterialCookState.Failed,
                OperationId: null);
        }
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

    [LoggerMessage(
        EventId = 1001,
        Level = LogLevel.Warning,
        Message = "Material cook failed for {MaterialUri} with {DiagnosticCount} diagnostics.")]
    private partial void LogMaterialCookFailed(Uri materialUri, int diagnosticCount);

    [LoggerMessage(
        EventId = 1002,
        Level = LogLevel.Warning,
        Message = "Material cook failed for {MaterialUri}.")]
    private partial void LogMaterialCookException(Uri materialUri, Exception exception);
}
