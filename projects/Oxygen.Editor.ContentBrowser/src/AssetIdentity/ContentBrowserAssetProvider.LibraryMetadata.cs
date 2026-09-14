// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Queues library metadata refreshes after catalog changes without blocking browser status projection.</summary>
public sealed partial class ContentBrowserAssetProvider
{
    private Task? libraryMetadataRefresh;
    private ProjectContext? libraryMetadataProject;
    private long libraryMetadataRevision = -1;

    private void RefreshLibraryMetadata(ProjectContext? project, long revision)
    {
        if (project is null || project.LocalFolderMounts.Count == 0 || this.cookStatus is not ICookedLibraryMetadataService metadata)
        {
            return;
        }

        lock (this.refreshSync)
        {
            if (this.disposed || this.libraryMetadataRefresh?.IsCompleted == false
                || (ReferenceEquals(this.libraryMetadataProject, project) && this.libraryMetadataRevision == revision))
            {
                return;
            }

            this.libraryMetadataProject = project;
            this.libraryMetadataRevision = revision;
            var cancellationToken = this.lifetime.Token;
            this.libraryMetadataRefresh = Task.Run(() => this.RefreshLibraryMetadataAsync(metadata, project, revision, cancellationToken), CancellationToken.None);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "This background task has no awaiting caller; observe and log every failure while the coordinator owns native cleanup.")]
    private async Task RefreshLibraryMetadataAsync(ICookedLibraryMetadataService metadata, ProjectContext project, long revision, CancellationToken cancellationToken)
    {
        try
        {
            var changed = await metadata.RefreshLibraryMetadataAsync(project, cancellationToken).ConfigureAwait(false);
            lock (this.refreshSync)
            {
                if (changed && !this.disposed && ReferenceEquals(project, this.projectContextService.ActiveProject))
                {
                    _ = this.RequestRefresh(invalidate: false);
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception error)
        {
            this.LogLibraryMetadataRefreshFailed(error);
        }
        finally
        {
            lock (this.refreshSync)
            {
                this.libraryMetadataRefresh = null;
                if (!this.disposed && this.projectContextService.ActiveProject is { } current
                    && (!ReferenceEquals(current, project) || this.catalogRevision != revision))
                {
                    this.RefreshLibraryMetadata(current, this.catalogRevision);
                }
            }
        }
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "Could not refresh cooked-library dependency metadata.")]
    private partial void LogLibraryMetadataRefreshFailed(Exception error);
}
