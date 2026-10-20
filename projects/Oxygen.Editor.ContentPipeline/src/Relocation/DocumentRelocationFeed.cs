// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>The relocations of the active project's assets, as open documents follow them.</summary>
public sealed class DocumentRelocationFeed : IDocumentRelocations, IDisposable
{
    private readonly IAssetRelocationService relocation;
    private readonly IProjectContextService projects;

    /// <summary>Initializes a new instance of the <see cref="DocumentRelocationFeed"/> class.</summary>
    /// <param name="relocation">The service that relocates the project's assets.</param>
    /// <param name="projects">The active project, whose content folders hold the documents' files.</param>
    public DocumentRelocationFeed(IAssetRelocationService relocation, IProjectContextService projects)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        ArgumentNullException.ThrowIfNull(projects);
        this.relocation = relocation;
        this.projects = projects;
        this.relocation.FilesChanged += this.OnFilesChanged;
    }

    /// <inheritdoc/>
    public event EventHandler<DocumentFilesDeletedEventArgs>? FilesDeleted;

    /// <inheritdoc/>
    public IDisposable Follow(Func<Func<Document, DocumentRelocation?>, Task> follow)
    {
        ArgumentNullException.ThrowIfNull(follow);
        return this.relocation.AddParticipant(new Follower(this.projects, follow));
    }

    /// <inheritdoc/>
    public void Dispose() => this.relocation.FilesChanged -= this.OnFilesChanged;

    private void OnFilesChanged(object? sender, AssetFilesChangedEventArgs args)
    {
        if (args.DeletedFiles.Count != 0)
        {
            this.FilesDeleted?.Invoke(this, new DocumentFilesDeletedEventArgs(args.DeletedFiles));
        }
    }

    private sealed class Follower(IProjectContextService projects, Func<Func<Document, DocumentRelocation?>, Task> follow)
        : IAssetRelocationParticipant
    {
        public Task FollowAsync(AssetRelocationChange change)
            => projects.ActiveProject is { } project
                ? follow(document => DocumentRelocations.Map(document, change, project, out _))
                : Task.CompletedTask;
    }
}
