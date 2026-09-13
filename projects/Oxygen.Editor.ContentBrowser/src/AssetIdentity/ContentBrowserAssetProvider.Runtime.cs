// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Projects acknowledged native availability without recooking or scanning output on runtime events.</summary>
public sealed partial class ContentBrowserAssetProvider
{
    private static (AssetRuntimeAvailability state, string? reason) GetRuntimeAvailability(ContentBrowserAssetItem item, RuntimeContentSnapshot content, RuntimeAssetRequestStatus[] requests)
    {
        if (item.Kind is not (AssetKind.Material or AssetKind.Geometry or AssetKind.Scene))
        {
            return (AssetRuntimeAvailability.NotApplicable, null);
        }

        if (content.State is RuntimeContentState.Unavailable or RuntimeContentState.Failed || content.RunId == Guid.Empty)
        {
            return (AssetRuntimeAvailability.Unavailable, content.Reason ?? "The preview is not running.");
        }

        if (content.State == RuntimeContentState.Updating)
        {
            return (AssetRuntimeAvailability.Updating, "The preview is updating cooked content.");
        }

        var paths = new[] { item.IdentityUri, item.CookedUri, item.BuiltinOriginUri }.OfType<Uri>().Select(AssetUriHelper.GetVirtualPath).Select(NormalizeRuntimePath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var applicable = requests.Where(request => AssetPath(request.Request.Command) is { } path && paths.Contains(NormalizeRuntimePath(path))).ToArray();
        if (applicable.FirstOrDefault(static request => request.Succeeded == false) is { } failed)
        {
            return (AssetRuntimeAvailability.Failed, failed.Reason ?? "The asset could not be applied in the preview.");
        }

        if (applicable.Any(static request => request.Succeeded is null))
        {
            return (AssetRuntimeAvailability.Updating, "The asset is loading in the preview.");
        }

        if (item.IsBuiltin && item.BuiltinOriginUri is null)
        {
            return item.Generated?.IsLastKnown == true
                ? (AssetRuntimeAvailability.Unavailable, "The engine catalog is last-known. Preview unavailable.")
                : (AssetRuntimeAvailability.Mounted, null);
        }

        var mounted = content.State == RuntimeContentState.Mounted && item.CookedPath is { } cookedPath && content.Roots.Any(root => Path.GetFullPath(cookedPath).StartsWith(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase));
        return mounted ? (AssetRuntimeAvailability.Mounted, null) : (AssetRuntimeAvailability.NotMounted, "This asset's cooked content is not mounted in the preview.");
    }

    private static string? AssetPath(RuntimeWorldCommand command) => command switch
    {
        RuntimeSetGeometry geometry => geometry.AssetPath,
        RuntimeSetMaterialOverride material => material.MaterialPath,
        _ => null,
    };

    private static string NormalizeRuntimePath(string path)
    {
        if (Uri.TryCreate(path, UriKind.Absolute, out var uri) && string.Equals(uri.Scheme, "asset", StringComparison.OrdinalIgnoreCase))
        {
            path = AssetUriHelper.GetVirtualPath(uri);
        }

        path = path.Replace('\\', '/').Trim('/');
        return path.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? path[..^5] : path;
    }

    private void OnRuntimeContentChanged(object? sender, RuntimeContentChangedEventArgs args) => this.RefreshRuntimeState();

    private void OnRuntimeStateChanged(object? sender, EngineStateChangedEventArgs args) => this.RefreshRuntimeState();

    private void OnRuntimeAssetStatusChanged(object? sender, EventArgs args) => this.RefreshRuntimeState();

    private void RefreshRuntimeState()
    {
        lock (this.refreshSync)
        {
            if (!this.disposed)
            {
                this.PublishLiveState();
            }
        }
    }

    private ContentBrowserAssetItem[] ApplyRuntimeState(IReadOnlyList<ContentBrowserAssetItem> items, ProjectContext? project)
    {
        var content = this.engine.ContentStatus;
        var requests = this.runtimeWorld.AssetRequests.Where(request => request.Request.Target.RunId == content.RunId && request.Request.Target.ProjectId == project?.ProjectId).ToArray();
        return items.Select(item =>
        {
            var (availability, reason) = GetRuntimeAvailability(item, content, requests);
            return item.RuntimeAvailability == availability && string.Equals(item.RuntimeReason, reason, StringComparison.Ordinal)
                ? item : item with { RuntimeAvailability = availability, RuntimeReason = reason };
        }).ToArray();
    }
}
