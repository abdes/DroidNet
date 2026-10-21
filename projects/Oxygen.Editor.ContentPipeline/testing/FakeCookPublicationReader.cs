// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>
///     An <see cref="ICookPublicationReader"/> for tests: the test states which roots are published, in precedence
///     order, where each project mount's output is, and which products the project output holds. Each root serves the
///     index document it was given. It never touches the pipeline's format on disk.
/// </summary>
/// <param name="projectId">The project the publication belongs to.</param>
/// <param name="projectRoot">The project's root folder.</param>
public sealed class FakeCookPublicationReader(Guid projectId, FileSystemPath projectRoot) : ICookPublicationReader
{
    private Selection current = new(PublicationId: null, [], ImmutableDictionary<string, FileSystemPath>.Empty, []);

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
    {
        ArgumentNullException.ThrowIfNull(document);
        return new(new(owner, name, document.SourceGuid, new string('0', 64), owner == CookPublicationRootOwner.Library ? folder.FullPath : null), document, folder);
    }

    /// <summary>Selects a new publication of the given roots, in precedence order.</summary>
    /// <param name="roots">The published roots; a later root overrides an earlier one.</param>
    /// <param name="projectRoots">The physical output folder of each project mount.</param>
    /// <param name="projectOutputs">The virtual paths of the products the project output holds.</param>
    public void Publish(IReadOnlyList<PublishedRoot> roots, IReadOnlyDictionary<string, FileSystemPath>? projectRoots = null, IEnumerable<string>? projectOutputs = null)
    {
        ArgumentNullException.ThrowIfNull(roots);
        this.current = new(
            Guid.NewGuid(),
            roots,
            (projectRoots ?? new Dictionary<string, FileSystemPath>()).ToImmutableDictionary(StringComparer.OrdinalIgnoreCase),
            [.. projectOutputs ?? []]);
    }

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
    public sealed record PublishedRoot(CookPublicationRoot Binding, Document Document, FileSystemPath Folder);

    private sealed record Selection(
        Guid? PublicationId,
        IReadOnlyList<PublishedRoot> Roots,
        ImmutableDictionary<string, FileSystemPath> ProjectRoots,
        ImmutableArray<string> ProjectOutputPaths);

    private sealed class ReadLease : ICookPublicationReadLease
    {
        private readonly FakeCookPublicationReader owner;
        private readonly Selection selection;
        private bool disposed;

        public ReadLease(FakeCookPublicationReader owner, Selection selection)
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

        public IEnumerable<string> ProjectOutputPaths => this.selection.ProjectOutputPaths;

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
