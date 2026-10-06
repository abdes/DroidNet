// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns one candidate's write phases and its final protected verification opening.</summary>
internal sealed class CookStagingRoot(string mount, Guid sourceKey, string path) : IAsyncDisposable
{
    private CookOutputReadLease? verification;
    private CookedInventoryReport? inventory;
    private bool sealedRoot;
    private bool disposed;

    internal string Mount { get; } = mount;

    internal Guid SourceKey { get; } = sourceKey;

    internal string Path { get; } = path;

    internal CookRootImage Before { get; private set; } = new(Exists: false, ImmutableDictionary<string, CookRootImage.FileImage>.Empty);

    internal async Task SeedAsync(string previousRoot, CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        this.Before = await CookRootImage.CaptureAsync(previousRoot, this.Path, cancellationToken, excludeGenerationMarker: true).ConfigureAwait(false);
    }

    internal CookedInventoryReport Inventory => this.inventory
        ?? throw new InvalidOperationException("The candidate root has not passed native validation.");

    internal async Task PrepareWriteAsync()
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (this.sealedRoot)
        {
            throw new InvalidOperationException("An immutable cooked generation cannot be reopened for writing.");
        }

        var previous = this.verification;
        this.verification = null;
        this.inventory = null;
        if (previous is not null)
        {
            await previous.DisposeAsync().ConfigureAwait(false);
        }
    }

    internal void AcceptVerification(CookOutputReadLease opening, CookedInventoryReport result)
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (this.sealedRoot || this.verification is not null || !result.IsValid || result.SourceKey != this.SourceKey)
        {
            throw new InvalidDataException("Native output did not validate the candidate's fresh generation identity.");
        }

        _ = opening.GetFiles();
        this.inventory = result;
        this.verification = opening;
    }

    internal CookPublicationRoot Seal()
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        var opening = this.verification ?? throw new InvalidOperationException("Only validated output can be sealed.");
        _ = opening.GetFiles();
        if (!this.sealedRoot)
        {
            var marker = System.IO.Path.Combine(this.Path, CookedGeneration.MarkerFileName);
            using (var created = new FileStream(marker, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
            {
                this.sealedRoot = true;
                created.Flush(flushToDisk: true);
            }
        }

        opening.RetainGeneration();

        return new(CookPublicationRootOwner.Project, this.Mount, this.SourceKey, this.Inventory.IndexSha256, LibraryPath: null);
    }

    public async ValueTask DisposeAsync()
    {
        this.disposed = true;
        var opening = this.verification;
        this.verification = null;
        if (opening is not null)
        {
            await opening.DisposeAsync().ConfigureAwait(false);
        }
    }
}
