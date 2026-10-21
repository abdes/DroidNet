// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A captured publication and the lifetime ownership of its immutable document and generations.</summary>
public sealed class CookPublicationReadLease : ICookPublicationReadLease
{
    private readonly SharedState state;
    private bool disposed;

    private CookPublicationReadLease(SharedState state) => this.state = state;

    /// <summary>Gets the selected publication identity, or null before the first publication.</summary>
    public Guid? PublicationId => this.state.Document?.OperationId;

    public Guid ProjectId => this.state.Products.ProjectId;

    public FileSystemPath ProjectRoot => this.state.ProjectRoot;

    /// <summary>Gets the accepted roots in native precedence order.</summary>
    public ImmutableArray<CookPublicationRoot> Roots => this.state.Document?.Roots ?? [];

    /// <summary>Gets the physical paths corresponding to the accepted root order.</summary>
    public ImmutableArray<string> RootPaths => this.state.RootPaths;

    /// <summary>Gets the captured physical generations by logical project mount.</summary>
    public ImmutableDictionary<string, FileSystemPath> ProjectRoots => this.state.ProjectRoots;

    /// <inheritdoc />
    public IEnumerable<string> ProjectOutputPaths => this.state.Products.Products
        .SelectMany(static product => product.Outputs).Select(static output => output.Asset.VirtualPath);

    /// <inheritdoc />
    public ImmutableArray<CookPublicationRoot> UnavailableRoots => this.state.UnavailableRoots;

    /// <summary>Gets the roots a runtime mounts, with their physical paths, in native precedence order: every root but the unavailable ones.</summary>
    public IEnumerable<(CookPublicationRoot Root, string Path)> MountableRoots
        => this.Roots.Zip(this.RootPaths).Where(entry => !this.UnavailableRoots.Contains(entry.First));

    internal CookPublicationDocument? Document => this.state.Document;

    internal FileSnapshot Head => this.state.Head;

    internal CookProvenance ProductState => this.state.Products;

    /// <summary>Resolves an owned logical mount from this captured publication.</summary>
    /// <param name="mount">The project authoring mount.</param>
    /// <returns>The selected generation path, or null when that mount has no cooked output.</returns>
    public FileSystemPath? FindProjectRoot(string mount) => this.state.ProjectRoots.GetValueOrDefault(mount);

    /// <summary>Creates a fixed catalog of a loose root after matching one index opening to its captured root binding.</summary>
    /// <param name="root">A loose root from this publication.</param>
    /// <param name="cancellationToken">Cancels reading the index; payloads are not hashed.</param>
    /// <returns>A catalog whose records remain tied to this publication.</returns>
    /// <exception cref="CookedLibraryChangedException">A library root's index was replaced after this publication was captured.</exception>
    /// <exception cref="InvalidDataException">The project's own root no longer matches this publication.</exception>
    public async Task<Oxygen.Managed.Assets.Catalog.LooseCooked.LooseCookedIndexAssetCatalog> CreateCatalogAsync(
        CookPublicationRoot root, CancellationToken cancellationToken)
    {
        using var retained = this.Retain();
        if (!this.Roots.Contains(root) || this.UnavailableRoots.Contains(root) || root.Container != CookPublicationRootContainer.LooseFolder)
        {
            throw new InvalidDataException("The catalog root is not an available loose root of this publication.");
        }

        var path = root.ResolvePath(this.ProjectRoot.FullPath);
        var index = (await CookedIndexSnapshot.ReadAsync(path, cancellationToken).ConfigureAwait(false)).RequireCurrent(path);
        if (index.Index.SourceGuid != root.SourceKey || !string.Equals(index.Fingerprint, root.IndexSha256, StringComparison.OrdinalIgnoreCase))
        {
            throw CookedLibraryChangedException.ForChangedRoot(root, path, index.Index.SourceGuid, index.Fingerprint);
        }

        return new(index.Index, FileSystemPath.From(path));
    }

    /// <summary>
    /// Requires sealed owned roots before native mounting or publication, including disk-only publication. A root cooked in
    /// another index format is not a broken generation: it is left unmounted until it is recooked.
    /// </summary>
    internal void RequireAvailableGenerations()
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (!this.state.UnsealedRoots.IsEmpty)
        {
            throw new InvalidDataException("Cooked generations need rebuilding: "
                + string.Join(", ", this.state.UnsealedRoots.Select(static root => root.Name)) + ".");
        }
    }

    /// <summary>Retains this exact snapshot without reopening the head or reacquiring filesystem handles.</summary>
    /// <returns>Independent disposal ownership of the same immutable selection.</returns>
    public CookPublicationReadLease Retain()
    {
        lock (this.state.Sync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            var retained = new CookPublicationReadLease(this.state);
            checked { this.state.Readers++; }
            return retained;
        }
    }

    /// <inheritdoc />
    ICookPublicationReadLease ICookPublicationReadLease.Retain() => this.Retain();

    /// <inheritdoc />
    public async Task<IReadOnlyList<Oxygen.Managed.Assets.Catalog.IAssetCatalog>> CreateCatalogsAsync(CancellationToken cancellationToken)
    {
        var catalogs = new List<Oxygen.Managed.Assets.Catalog.IAssetCatalog>(this.Roots.Length);
        try
        {
            foreach (var root in this.Roots.Where(root => !this.UnavailableRoots.Contains(root)))
            {
                catalogs.Add(root.Container == CookPublicationRootContainer.PakFile
                    ? await this.CreatePakCatalogAsync(root, cancellationToken).ConfigureAwait(false)
                    : await this.CreateCatalogAsync(root, cancellationToken).ConfigureAwait(false));
            }

            return catalogs;
        }
        catch
        {
            foreach (var catalog in catalogs)
            {
                (catalog as IDisposable)?.Dispose();
            }

            throw;
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        List<FileStream>? release = null;
        lock (this.state.Sync)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            if (--this.state.Readers == 0)
            {
                release = this.state.Handles;
            }
        }

        if (release is not null)
        {
            foreach (var handle in release.AsEnumerable().Reverse())
            {
                handle.Dispose();
            }
        }
    }

    /// <summary>Returns a lease handed back to the pipeline as the lease the pipeline issued.</summary>
    /// <param name="lease">A lease from <see cref="ICookPublicationReader"/>.</param>
    /// <returns>The pipeline's own lease.</returns>
    /// <exception cref="ArgumentException">The lease was not issued by the content pipeline.</exception>
    internal static CookPublicationReadLease Issued(ICookPublicationReadLease lease)
    {
        ArgumentNullException.ThrowIfNull(lease);
        return lease as CookPublicationReadLease
            ?? throw new ArgumentException("The publication lease was not issued by the content pipeline.", nameof(lease));
    }

    /// <summary>Captures the selected document after recovery, while the caller owns the selection gate.</summary>
    internal static async Task<CookPublicationReadLease> OpenUnderGateAsync(ProjectContext project, IAtomicFileStore files,
        CookOutputWriteLease gate, CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        var head = await files.ReadAsync(CookPublicationPaths.Head(project.ProjectRoot.FullPath), cancellationToken).ConfigureAwait(false);
        return await OpenDocumentUnderGateAsync(project, files, gate, head, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Retains a known candidate or previous selection without rereading the mutable head.</summary>
    internal static async Task<CookPublicationReadLease> OpenDocumentUnderGateAsync(ProjectContext project, IAtomicFileStore files,
        CookOutputWriteLease gate, FileSnapshot selection, CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        if (!selection.Version.Exists)
        {
            return new(new(project, selection, document: null, [], [], []));
        }

        var head = JsonSerializer.Deserialize<CookPublicationHead>(selection.Content.AsSpan(), CookPublicationDocument.JsonOptions);
        if (head is null || head.Version != CookPublicationHead.CurrentVersion || head.PublicationId == Guid.Empty
            || !CookPublicationDocument.IsDigest(head.DocumentSha256))
        {
            throw new InvalidDataException("The cooked publication head is invalid.");
        }

        var handles = new List<FileStream>(capacity: 1);
        try
        {
            var path = CookPublicationPaths.Document(project.ProjectRoot.FullPath, head.PublicationId);

            // The immutable document cannot be replaced while its contents are
            // read or a consumer still retains this selection.
            handles.Add(new(path, FileMode.Open, FileAccess.Read, FileShare.Read));
            var bytes = await files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
            if (!bytes.Version.Exists || !string.Equals(bytes.Version.Sha256, head.DocumentSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("The publication document does not match the selected digest.");
            }

            var document = CookPublicationDocument.Parse(bytes.Content.AsSpan());
            document.Validate(project);
            if (document.OperationId != head.PublicationId)
            {
                throw new InvalidDataException("The publication document has a different identity than its head.");
            }

            handles.EnsureCapacity(checked(document.Roots.Length + 1));
            var unavailable = ImmutableArray.CreateBuilder<CookPublicationRoot>(document.Roots.Length);
            var unsealed = ImmutableArray.CreateBuilder<CookPublicationRoot>();
            foreach (var root in document.Roots)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var marker = Path.Combine(root.ResolvePath(project.ProjectRoot.FullPath), CookedGeneration.MarkerFileName);
                CookOutputLease.RejectReparsePoint(marker);
                var reader = WindowsCookFile.TryOpenGenerationReader(marker, out var busy);
                if (reader is not null)
                {
                    handles.Add(reader);
                }
                else if (busy)
                {
                    throw new CookOutputBusyException($"Cooked generation '{root.Name}' is being reclaimed. Refresh the publication.");
                }
                else if (root.Owner == CookPublicationRootOwner.Project)
                {
                    unavailable.Add(root);
                    unsealed.Add(root);
                    continue;
                }

                if (await IsStaleAsync(root, project, cancellationToken).ConfigureAwait(false))
                {
                    unavailable.Add(root);
                }
            }

            return new(new(project, selection, document, handles, unavailable.ToImmutable(), unsealed.ToImmutable()));
        }
        catch
        {
            foreach (var handle in handles.AsEnumerable().Reverse())
            {
                handle.Dispose();
            }

            throw;
        }
    }

    // A loose root cooked in another index format has nothing readable until it is recooked. The read is shared with
    // every later consumer of the same index; a missing or damaged index is reported by the consumer that needs it.
    private static async Task<bool> IsStaleAsync(CookPublicationRoot root, ProjectContext project, CancellationToken cancellationToken)
    {
        if (root.Container != CookPublicationRootContainer.LooseFolder)
        {
            return false;
        }

        try
        {
            return await CookedIndexSnapshot.ReadAsync(root.ResolvePath(project.ProjectRoot.FullPath), cancellationToken).ConfigureAwait(false) is CookedIndexRead.Stale;
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    // A pak's catalog is immutable once read; the publication recorded the identity and catalog digest it must match.
    private async Task<Oxygen.Managed.Assets.Catalog.Pak.PakAssetCatalog> CreatePakCatalogAsync(CookPublicationRoot root, CancellationToken cancellationToken)
    {
        var path = FileSystemPath.From(root.ResolvePath(this.ProjectRoot.FullPath));
        using var pak = await PakReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
        if (pak.Document.SourceIdentity != root.SourceKey || !string.Equals(pak.Document.CatalogDigest, root.IndexSha256, StringComparison.OrdinalIgnoreCase))
        {
            throw CookedLibraryChangedException.ForChangedRoot(root, path.FullPath, pak.Document.SourceIdentity, pak.Document.CatalogDigest);
        }

        return new(pak.Document, path);
    }

    private sealed class SharedState
    {
        internal SharedState(ProjectContext project, FileSnapshot head, CookPublicationDocument? document, List<FileStream> handles,
            ImmutableArray<CookPublicationRoot> unavailableRoots, ImmutableArray<CookPublicationRoot> unsealedRoots)
        {
            this.Head = head;
            this.ProjectRoot = project.ProjectRoot;
            this.Document = document;
            this.Handles = handles;
            this.UnavailableRoots = unavailableRoots;
            this.UnsealedRoots = unsealedRoots;
            var roots = document?.Roots ?? [];
            this.RootPaths = [.. roots.Select(root => root.ResolvePath(project.ProjectRoot.FullPath))];
            this.ProjectRoots = roots.Where(static root => root.Owner == CookPublicationRootOwner.Project)
                .ToImmutableDictionary(static root => root.Name, root => FileSystemPath.From(root.ResolvePath(project.ProjectRoot.FullPath)), StringComparer.Ordinal);
            this.Products = document?.ProductState ?? new(project.ProjectId, [], []);
        }

        internal Lock Sync { get; } = new();

        internal int Readers { get; set; } = 1;

        internal FileSnapshot Head { get; }

        internal FileSystemPath ProjectRoot { get; }

        internal CookPublicationDocument? Document { get; }

        internal ImmutableArray<string> RootPaths { get; }

        internal ImmutableArray<CookPublicationRoot> UnavailableRoots { get; }

        // Owned roots whose generation marker is missing; they must be rebuilt into fresh generations.
        internal ImmutableArray<CookPublicationRoot> UnsealedRoots { get; }

        internal ImmutableDictionary<string, FileSystemPath> ProjectRoots { get; }

        internal CookProvenance Products { get; }

        internal List<FileStream> Handles { get; }
    }
}
