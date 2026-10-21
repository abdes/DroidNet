// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Assets;

/// <summary>Projects acknowledged native availability without recooking or scanning output on runtime events.</summary>
public sealed partial class AssetFeed
{
    private static (AssetRuntimeAvailability State, string? Reason) GetRuntimeAvailability(AssetEntry item, AssetRuntimeStatus status)
    {
        if (item.Kind is not (AssetKind.Material or AssetKind.Geometry or AssetKind.Scene))
        {
            return (AssetRuntimeAvailability.NotApplicable, null);
        }

        if (status.State == AssetRuntimeContentState.Unavailable)
        {
            return (AssetRuntimeAvailability.Unavailable, status.Reason ?? "The preview is not running.");
        }

        if (status.State == AssetRuntimeContentState.Updating)
        {
            return (AssetRuntimeAvailability.Updating, "The preview is updating cooked content.");
        }

        var applicable = status.Requests.Where(request => request.Asset == item.Identity).ToArray();
        if (applicable.FirstOrDefault(static request => request.Succeeded == false) is { } failed)
        {
            return (AssetRuntimeAvailability.Failed, failed.Reason ?? "The asset could not be applied in the preview.");
        }

        if (applicable.Any(static request => request.Succeeded is null))
        {
            return (AssetRuntimeAvailability.Updating, "The asset is loading in the preview.");
        }

        // Every runtime mounts the engine content pak as its permanent base.
        if (item is { Origin: AssetOrigin.Engine, IsPackaged: true })
        {
            return (AssetRuntimeAvailability.Mounted, null);
        }

        var mounted = status.State == AssetRuntimeContentState.Mounted && item.CookedPath is { } cookedPath && status.Roots.Any(cookedPath.IsSameOrInside);
        return mounted ? (AssetRuntimeAvailability.Mounted, null) : (AssetRuntimeAvailability.NotMounted, "This asset's cooked content is not mounted in the preview.");
    }

    private void OnRuntimeStatusChanged(object? sender, EventArgs args)
    {
        lock (this.refreshSync)
        {
            if (!this.disposed)
            {
                this.PublishLiveState();
            }
        }
    }

    private AssetEntry[] ApplyRuntimeState(IReadOnlyList<AssetEntry> items, ProjectContext? project)
    {
        var status = this.runtimeStatus.GetStatus(project?.ProjectId);
        return [.. items.Select(item =>
        {
            var (availability, reason) = GetRuntimeAvailability(item, status);
            return item.RuntimeAvailability == availability && string.Equals(item.RuntimeReason, reason, StringComparison.Ordinal)
                ? item : item with { RuntimeAvailability = availability, RuntimeReason = reason };
        })];
    }
}
