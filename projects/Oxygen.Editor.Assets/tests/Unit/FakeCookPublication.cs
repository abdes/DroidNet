// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>
///     A controlled cook publication for asset tests: the test states which roots are published, in precedence
///     order, and each root serves the index document it was given. It never touches the pipeline's format.
/// </summary>
/// <param name="projectId">The project the publication belongs to.</param>
/// <param name="projectRoot">The project's root folder.</param>
internal sealed class FakeCookPublication(Guid projectId, FileSystemPath projectRoot) : ICookPublicationReader
{
    private Selection current = new(null, [], ImmutableDictionary<string, FileSystemPath>.Empty);

    /// <summary>Gets the project the publication belongs to.</summary>
    public Guid ProjectId { get; } = projectId;

    /// <summary>Gets the project's root folder.</summary>
    public FileSystemPath ProjectRoot { get; } = projectRoot;

    /// <summary>Gets how many leases are currently held.</summary>
    public int HeldLeases { get; private set; }

    /// <summary>Creates a root binding for a published folder.</summary>
    /// <param name="owner">Whether the project or a library owns the root.</param>
    /// <param name="name">The mount or library name.</param>
    /// <param name="document">The index the root serves.</param>
    /// <param name="folder">The root's physical folder.</param>
    /// <returns>A root and the index it serves.</returns>
    public static PublishedRoot Root(CookPublicationRootOwner owner, string name, Document document, FileSystemPath folder)
        => new(new(owner, name, document.SourceGuid, new string('0', 64), owner == CookPublicationRootOwner.Library ? folder.FullPath : null), document, folder);

    /// <summary>Selects a new publication of the given roots, in precedence order.</summary>
    /// <param name="roots">The published roots; a later root overrides an earlier one.</param>
    /// <param name="projectRoots">The physical output folder of each project mount.</param>
    public void Publish(IReadOnlyList<PublishedRoot> roots, IReadOnlyDictionary<string, FileSystemPath>? projectRoots = null)
        => this.current = new(Guid.NewGuid(), roots, (projectRoots ?? new Dictionary<string, FileSystemPath>()).ToImmutableDictionary(StringComparer.OrdinalIgnoreCase));

    /// <summary>Creates a lease on the current publication without going through a reader.</summary>
    /// <returns>A lease the test owns.</returns>
    public ICookPublicationReadLease Lease() => new ReadLease(this, this.current);

    /// <inheritdoc />
    public Task<ICookPublicationReadLease> AcquireReadAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(this.Lease());
    }

    /// <inheritdoc />
    public async Task<CookPublicationCatalogs> AcquireCatalogsAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        var lease = this.Lease();
        return new(lease, await lease.CreateCatalogsAsync(cancellationToken).ConfigureAwait(false));
    }

    /// <summary>A root binding and the index it serves.</summary>
    /// <param name="Binding">The published root.</param>
    /// <param name="Document">The root's index.</param>
    /// <param name="Folder">The root's physical folder.</param>
    internal sealed record PublishedRoot(CookPublicationRoot Binding, Document Document, FileSystemPath Folder);

    private sealed record Selection(
        Guid? PublicationId,
        IReadOnlyList<PublishedRoot> Roots,
        ImmutableDictionary<string, FileSystemPath> ProjectRoots);

    private sealed class ReadLease : ICookPublicationReadLease
    {
        private readonly FakeCookPublication owner;
        private readonly Selection selection;
        private bool disposed;

        public ReadLease(FakeCookPublication owner, Selection selection)
        {
            this.owner = owner;
            this.selection = selection;
            owner.HeldLeases++;
        }

        public Guid? PublicationId => this.selection.PublicationId;

        public Guid ProjectId => this.owner.ProjectId;

        public FileSystemPath ProjectRoot => this.owner.ProjectRoot;

        public ImmutableArray<CookPublicationRoot> Roots => [.. this.selection.Roots.Select(static root => root.Binding)];

        public ImmutableArray<CookPublicationRoot> UnavailableRoots => [];

        public ImmutableDictionary<string, FileSystemPath> ProjectRoots => this.selection.ProjectRoots;

        public Task<IReadOnlyList<IAssetCatalog>> CreateCatalogsAsync(CancellationToken cancellationToken)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult<IReadOnlyList<IAssetCatalog>>(
                [.. this.selection.Roots.Select(static root => new LooseCookedIndexAssetCatalog(root.Document, root.Folder))]);
        }

        public ICookPublicationReadLease Retain()
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            return new ReadLease(this.owner, this.selection);
        }

        public void Dispose()
        {
            if (!this.disposed)
            {
                this.disposed = true;
                this.owner.HeldLeases--;
            }
        }
    }
}
