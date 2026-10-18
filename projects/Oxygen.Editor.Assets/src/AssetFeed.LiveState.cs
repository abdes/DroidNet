// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets;

/// <summary>Updates authoring and operation overlays without rereading content on each edit or progress message.</summary>
public sealed partial class AssetFeed
{
    private readonly HashSet<Guid> completedStatusRuns = [];

    private static bool Includes(CookRunSnapshot run, AssetEntry item)
    {
        // The run keys its assets by source file; an asset is the identity its sources stand for.
        if (run.Assets.Keys.Any(source => AssetFileTypes.ToIdentity(source) == item.Identity)
            || (item.ImportSource is { } imported && (run.Request.ScopeUri == imported || run.Assets.ContainsKey(imported))))
        {
            return true;
        }

        var scope = run.Request.ScopeUri;
        return run.Request.TargetKind switch
        {
            CookTargetKind.Project => true,
            CookTargetKind.Folder => scope is not null && item.Identity != scope && item.Identity.IsSameOrInside(scope),
            _ => scope is not null && AssetFileTypes.ToIdentity(scope) == item.Identity,
        };
    }

    private static int ActivityPriority(CookRunSnapshot run)
        => run.IsCompleted ? 0 : run.State == CookRunState.Queued ? 1 : 2;

    private void OnDocumentStateChanged(object? sender, CookDocumentStateChangedEventArgs args)
    {
        lock (this.refreshSync)
        {
            if (this.disposed)
            {
                return;
            }

            this.PublishLiveState(args.SavedSourceChanged ? args.After!.SourcePath : null);
            if (args.SavedSourceChanged && this.projectContextService.ActiveProject is { } project
                && args.After!.SourcePath != project.ProjectRoot && args.After.SourcePath.IsSameOrInside(project.ProjectRoot))
            {
                _ = this.RequestRefresh(invalidate: true);
            }
        }
    }

    private void OnCookRunChanged(object? sender, CookRunChangedEventArgs args)
    {
        lock (this.refreshSync)
        {
            if (this.disposed || this.projectContextService.ActiveProject is not { } project
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

    private void PublishLiveState(FileSystemPath? changedSource = null)
    {
        var current = this.items.Value;
        var updated = this.ApplyLiveState(current, changedSource);
        if (current.Where((item, index) => !ReferenceEquals(item, updated[index])).Any())
        {
            this.items.OnNext(updated);
        }
    }

    private AssetEntry[] ApplyLiveState(IReadOnlyList<AssetEntry> source, FileSystemPath? changedSource = null)
    {
        var documentStates = this.documents.GetState().Documents;
        var project = this.projectContextService.ActiveProject;
        var runs = this.cooks.Runs.Where(run => run.ProjectId == project?.ProjectId && run.ProjectRoot == project.ProjectRoot)
            .OrderByDescending(ActivityPriority).ThenByDescending(static run => run.StartedAt).ToArray();
        var authored = source.Select(item =>
        {
            if (item.CookStatus is not { } status)
            {
                return item;
            }

            IReadOnlyList<FileSystemPath> paths = !status.SourcePaths.IsEmpty ? status.SourcePaths : item.DescriptorPath is { } descriptor ? [descriptor] : [];
            if (changedSource is not null && paths.Contains(changedSource)
                && status.Freshness == Oxygen.Editor.ContentPipeline.Status.AssetCookFreshness.Current)
            {
                status = status with { Freshness = Oxygen.Editor.ContentPipeline.Status.AssetCookFreshness.OutOfDate };
            }

            var dirty = documentStates.Where(document => document.IsDirty && paths.Contains(document.SourcePath)).ToArray();
            var run = runs.FirstOrDefault(candidate => Includes(candidate, item));
            var activity = run is null ? null : new AssetCookActivity(run.OperationId, run.State);
            return ReferenceEquals(status, item.CookStatus) && status.UnsavedDocuments.SequenceEqual(dirty) && item.CookActivity == activity
                ? item : ApplyCookStatus(item, status with { UnsavedDocuments = [.. dirty] }) with { CookActivity = activity };
        }).ToArray();
        return this.ApplyRuntimeState(authored, project);
    }
}
