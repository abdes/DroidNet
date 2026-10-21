// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets;

/// <summary>Updates authoring and operation overlays without rereading content on each edit or progress message.</summary>
public sealed partial class AssetFeed
{
    private readonly HashSet<Guid> completedStatusRuns = [];

    private static bool Includes(CookRunSnapshot run, AssetEntry item)
    {
        if (run.Assets.ContainsKey(item.Identity)
            || (item.ImportSource is { } imported && (run.Request.ScopeUri == imported || run.Assets.ContainsKey(imported))))
        {
            return true;
        }

        var scope = run.Request.ScopeUri;
        return run.Request.TargetKind switch
        {
            CookTargetKind.Project => true,
            CookTargetKind.Folder => scope is not null && item.Identity != scope && item.Identity.IsSameOrInside(scope),
            _ => scope == item.Identity,
        };
    }

    private static int ActivityPriority(CookRunSnapshot run)
        => run.IsCompleted ? 0 : run.State == CookRunState.Queued ? 1 : 2;

    // A save changes the file too; the catalog reports that change, which refreshes the status.
    private void OnUnsavedDocumentsChanged(object? sender, EventArgs args)
    {
        lock (this.refreshSync)
        {
            if (!this.disposed)
            {
                this.PublishLiveState();
            }
        }
    }

    private void OnCookRunChanged(object? sender, CookRunChangedEventArgs args)
    {
        lock (this.refreshSync)
        {
            if (this.disposed || this.SessionProject is not { } project
                || args.Run.ProjectId != project.ProjectId || args.Run.ProjectRoot != project.ProjectRoot)
            {
                return;
            }

            this.PublishLiveState();
            if (args.Run.IsCompleted && this.completedStatusRuns.Add(args.Run.OperationId))
            {
                _ = this.RequestRefresh(invalidate: true);
            }
        }
    }

    private void PublishLiveState()
    {
        var current = this.items.Value;
        var updated = this.ApplyLiveState(current);
        if (current.Where((item, index) => !ReferenceEquals(item, updated[index])).Any())
        {
            this.items.OnNext(updated);
        }
    }

    private AssetEntry[] ApplyLiveState(IReadOnlyList<AssetEntry> source)
    {
        var project = this.SessionProject;
        var runs = this.cooks.Runs.Where(run => run.ProjectId == project?.ProjectId && run.ProjectRoot == project.ProjectRoot)
            .OrderByDescending(ActivityPriority).ThenByDescending(static run => run.StartedAt).ToArray();
        var authored = source.Select(item =>
        {
            if (item.CookStatus is not { } status)
            {
                return item;
            }

            IReadOnlyList<FileSystemPath> paths = !status.SourcePaths.IsEmpty ? status.SourcePaths : item.DescriptorPath is { } descriptor ? [descriptor] : [];
            var dirty = this.documents.Find(paths);
            var run = runs.FirstOrDefault(candidate => Includes(candidate, item));
            var activity = run is null ? null : new AssetCookActivity(run.OperationId, run.State);
            return ReferenceEquals(status, item.CookStatus) && status.UnsavedDocuments.SequenceEqual(dirty) && item.CookActivity == activity
                ? item : ApplyCookStatus(item, status with { UnsavedDocuments = [.. dirty] }) with { CookActivity = activity };
        }).ToArray();
        return this.ApplyRuntimeState(authored, project);
    }
}
