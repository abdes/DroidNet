// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Updates authoring and operation overlays without rereading content on each edit or progress message.</summary>
public sealed partial class ContentBrowserAssetProvider
{
    private readonly HashSet<Guid> completedStatusRuns = [];

    private static bool Includes(CookRunSnapshot run, ContentBrowserAssetItem item)
    {
        if (run.Assets.ContainsKey(item.IdentityUri) || (item.ImportSourceUri is { } source && (run.Request.ScopeUri == source || run.Assets.ContainsKey(source))))
        {
            return true;
        }

        var scope = run.Request.ScopeUri;
        return run.Request.TargetKind switch
        {
            CookTargetKind.Project => true,
            CookTargetKind.Folder => scope is not null && item.IdentityUri.AbsolutePath.StartsWith(scope.AbsolutePath.TrimEnd('/') + "/", StringComparison.OrdinalIgnoreCase),
            _ => scope == item.IdentityUri,
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
                && args.After!.SourcePath.StartsWith(Path.GetFullPath(project.ProjectRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
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
                || args.Run.ProjectId != project.ProjectId || !string.Equals(args.Run.ProjectRoot, project.ProjectRoot, StringComparison.OrdinalIgnoreCase))
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

    private void PublishLiveState(string? changedSource = null)
    {
        var current = this.items.Value;
        var updated = this.ApplyLiveState(current, changedSource);
        if (current.Where((item, index) => !ReferenceEquals(item, updated[index])).Any())
        {
            this.items.OnNext(updated);
        }
    }

    private ContentBrowserAssetItem[] ApplyLiveState(IReadOnlyList<ContentBrowserAssetItem> source, string? changedSource = null)
    {
        var documentStates = this.documents.GetState().Documents;
        var project = this.projectContextService.ActiveProject;
        var runs = this.cooks.Runs.Where(run => run.ProjectId == project?.ProjectId && string.Equals(run.ProjectRoot, project?.ProjectRoot, StringComparison.OrdinalIgnoreCase))
            .OrderByDescending(ActivityPriority).ThenByDescending(static run => run.StartedAt).ToArray();
        var authored = source.Select(item =>
        {
            if (item.CookStatus is not { } status)
            {
                return item;
            }

            var paths = status.SourcePaths.IsEmpty ? new[] { item.DescriptorPath } : status.SourcePaths.ToArray();
            if (changedSource is not null && paths.Contains(changedSource, StringComparer.OrdinalIgnoreCase)
                && status.Freshness == Oxygen.Editor.ContentPipeline.Status.AssetCookFreshness.Current)
            {
                status = status with { Freshness = Oxygen.Editor.ContentPipeline.Status.AssetCookFreshness.OutOfDate };
            }

            var dirty = documentStates.Where(document => document.IsDirty && paths.Contains(document.SourcePath, StringComparer.OrdinalIgnoreCase)).ToArray();
            var run = runs.FirstOrDefault(candidate => Includes(candidate, item));
            var activity = run is null ? null : new AssetCookActivity(run.OperationId, run.State);
            return ReferenceEquals(status, item.CookStatus) && status.UnsavedDocuments.SequenceEqual(dirty) && item.CookActivity == activity
                ? item : ApplyCookStatus(item, status with { UnsavedDocuments = [.. dirty] }) with { CookActivity = activity };
        }).ToArray();
        return this.ApplyRuntimeState(authored, project);
    }
}
