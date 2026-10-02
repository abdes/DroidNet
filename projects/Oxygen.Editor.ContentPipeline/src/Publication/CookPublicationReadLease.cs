// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A captured publication and the lifetime ownership of its immutable document and generations.</summary>
public sealed class CookPublicationReadLease : IDisposable
{
    private readonly SharedState state;
    private bool disposed;

    private CookPublicationReadLease(SharedState state) => this.state = state;

    /// <summary>Gets the selected publication identity, or null before the first publication.</summary>
    public Guid? PublicationId => this.state.Document?.OperationId;

    public Guid ProjectId => this.state.Products.ProjectId;

    public string ProjectRoot => this.state.ProjectRoot;

    /// <summary>Gets the accepted roots in native precedence order.</summary>
    public ImmutableArray<CookPublicationRoot> Roots => this.state.Document?.Roots ?? [];

    /// <summary>Gets the physical paths corresponding to the accepted root order.</summary>
    public ImmutableArray<string> RootPaths => this.state.RootPaths;

    /// <summary>Gets the captured physical generations by logical project mount.</summary>
    public ImmutableDictionary<string, string> ProjectRoots => this.state.ProjectRoots;

    /// <summary>Gets logical product paths for project-output navigation, including named resources.</summary>
    public IEnumerable<string> ProjectOutputPaths => this.state.Products.Products
        .SelectMany(static product => product.Outputs).Select(static output => output.Asset.VirtualPath);

    /// <summary>Gets owned roots whose lifetime marker is missing and must be rebuilt into fresh generations.</summary>
    public ImmutableArray<CookPublicationRoot> UnavailableRoots => this.state.UnavailableRoots;

    internal CookPublicationDocument? Document => this.state.Document;

    internal FileSnapshot Head => this.state.Head;

    internal CookProvenance ProductState => this.state.Products;

    /// <summary>Resolves an owned logical mount from this captured publication.</summary>
    /// <param name="mount">The project authoring mount.</param>
    /// <returns>The selected generation path, or null when that mount has no cooked output.</returns>
    public string? FindProjectRoot(string mount) => this.state.ProjectRoots.GetValueOrDefault(mount);

    /// <summary>Creates a fixed catalog after matching one index opening to its captured root binding.</summary>
    /// <param name="root">A root from this publication.</param>
    /// <param name="cancellationToken">Cancels reading the index; payloads are not hashed.</param>
    /// <returns>A catalog whose records remain tied to this publication.</returns>
    public async Task<Oxygen.Managed.Assets.Catalog.LooseCooked.LooseCookedIndexAssetCatalog> CreateCatalogAsync(
        CookPublicationRoot root, CancellationToken cancellationToken)
    {
        using var retained = this.Retain();
        if (!this.Roots.Contains(root) || this.UnavailableRoots.Contains(root))
        {
            throw new InvalidDataException("The catalog root is not available in this publication.");
        }

        var path = root.ResolvePath(this.ProjectRoot);
        var index = await CookedIndexSnapshot.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        if (index.Index.SourceGuid != root.SourceKey || !string.Equals(index.Fingerprint, root.IndexSha256, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("The cooked index changed after publication. Refresh its library before using it.");
        }

        return new(index.Index, path);
    }

    /// <summary>Requires sealed owned roots before native mounting or publication, including disk-only publication.</summary>
    internal void RequireAvailableGenerations()
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (!this.UnavailableRoots.IsEmpty)
        {
            throw new InvalidDataException("Cooked generations need rebuilding: "
                + string.Join(", ", this.UnavailableRoots.Select(static root => root.Name)) + ".");
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

    /// <summary>Captures the selected document after recovery, while the caller owns the selection gate.</summary>
    internal static async Task<CookPublicationReadLease> OpenUnderGateAsync(ProjectContext project, IAtomicFileStore files,
        CookOutputWriteLease gate, CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        var head = await files.ReadAsync(CookPublicationPaths.Head(project.ProjectRoot), cancellationToken).ConfigureAwait(false);
        return await OpenDocumentUnderGateAsync(project, files, gate, head, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Retains a known candidate or previous selection without rereading the mutable head.</summary>
    internal static async Task<CookPublicationReadLease> OpenDocumentUnderGateAsync(ProjectContext project, IAtomicFileStore files,
        CookOutputWriteLease gate, FileSnapshot selection, CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        if (!selection.Version.Exists)
        {
            return new(new(project, selection, document: null, [], []));
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
            var path = CookPublicationPaths.Document(project.ProjectRoot, head.PublicationId);
            // The immutable document cannot be replaced while its contents are
            // read or a consumer still retains this selection.
            handles.Add(new(path, FileMode.Open, FileAccess.Read, FileShare.Read));
            var bytes = await files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
            if (!bytes.Version.Exists || !string.Equals(bytes.Version.Sha256, head.DocumentSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("The publication document does not match the selected digest.");
            }

            var document = JsonSerializer.Deserialize<CookPublicationDocument>(bytes.Content.AsSpan(), CookPublicationDocument.JsonOptions)
                ?? throw new InvalidDataException("The publication document is empty.");
            document.Validate(project);
            if (document.OperationId != head.PublicationId)
            {
                throw new InvalidDataException("The publication document has a different identity than its head.");
            }

            handles.EnsureCapacity(checked(document.Roots.Length + 1));
            var unavailable = ImmutableArray.CreateBuilder<CookPublicationRoot>(document.Roots.Length);
            foreach (var root in document.Roots)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var marker = Path.Combine(root.ResolvePath(project.ProjectRoot), CookedGeneration.MarkerFileName);
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
                }
            }

            return new(new(project, selection, document, handles, unavailable.ToImmutable()));
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

    private sealed class SharedState
    {
        internal SharedState(ProjectContext project, FileSnapshot head, CookPublicationDocument? document, List<FileStream> handles,
            ImmutableArray<CookPublicationRoot> unavailableRoots)
        {
            this.Head = head;
            this.ProjectRoot = Path.GetFullPath(project.ProjectRoot);
            this.Document = document;
            this.Handles = handles;
            this.UnavailableRoots = unavailableRoots;
            var roots = document?.Roots ?? [];
            this.RootPaths = [.. roots.Select(root => root.ResolvePath(project.ProjectRoot))];
            this.ProjectRoots = roots.Where(static root => root.Owner == CookPublicationRootOwner.Project)
                .ToImmutableDictionary(static root => root.Name, root => root.ResolvePath(project.ProjectRoot), StringComparer.OrdinalIgnoreCase);
            this.Products = document?.ProductState ?? new(project.ProjectId, [], []);
        }

        internal Lock Sync { get; } = new();

        internal int Readers { get; set; } = 1;

        internal FileSnapshot Head { get; }

        internal string ProjectRoot { get; }

        internal CookPublicationDocument? Document { get; }

        internal ImmutableArray<string> RootPaths { get; }

        internal ImmutableArray<CookPublicationRoot> UnavailableRoots { get; }

        internal ImmutableDictionary<string, string> ProjectRoots { get; }

        internal CookProvenance Products { get; }

        internal List<FileStream> Handles { get; }
    }
}
