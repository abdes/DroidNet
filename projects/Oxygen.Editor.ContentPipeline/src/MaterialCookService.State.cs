// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Read-only material output inspection for editor state queries.</summary>
public sealed partial class MaterialCookService
{
    /// <inheritdoc />
    public Task<MaterialCookState> GetMaterialCookStateAsync(
        Uri materialSourceUri,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(materialSourceUri);
        cancellationToken.ThrowIfCancellationRequested();

        if (this.projectContextService.ActiveProject is not { } project)
        {
            return Task.FromResult(MaterialCookState.NotCooked);
        }

        try
        {
            var input = CookInputResolver.Resolve(project, materialSourceUri, ContentCookInputRole.Primary);
            if (input.Kind != ContentCookAssetKind.Material)
            {
                return Task.FromResult(MaterialCookState.NotCooked);
            }

            var sourcePath = input.SourceAbsolutePath;
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
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or NotSupportedException or ArgumentException)
        {
            this.LogStateInspectionFailed(materialSourceUri, exception);
            return Task.FromResult(MaterialCookState.Failed);
        }
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
}
