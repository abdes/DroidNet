// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Keeps verified files protected from replacement until their native owner has finished.</summary>
public sealed class QualifiedArtifactLease : IDisposable, IAsyncDisposable
{
    private readonly Lock sync = new();
    private readonly ImmutableDictionary<string, string> paths;
    private ImmutableArray<FileStream> streams;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="QualifiedArtifactLease"/> class with verified file ownership.</summary>
    /// <param name="fingerprint">The canonical producer content identity.</param>
    /// <param name="paths">Verified physical paths by portable artifact identity.</param>
    /// <param name="streams">Protected file handles transferred by the verifier.</param>
    internal QualifiedArtifactLease(string fingerprint, ImmutableDictionary<string, string> paths, ImmutableArray<FileStream> streams)
    {
        this.Fingerprint = fingerprint;
        this.paths = paths;
        this.streams = streams;
    }

    /// <summary>Gets the artifact/schema content identity used by cook provenance and reuse.</summary>
    public string Fingerprint { get; }

    /// <summary>Gets a verified physical path while the lease remains active.</summary>
    /// <param name="artifactId">The required portable artifact identity.</param>
    /// <returns>The exact path whose bytes were verified.</returns>
    public string GetPath(string artifactId)
    {
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            return this.paths[artifactId];
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var stream in this.TakeStreams())
        {
            stream.Dispose();
        }
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        foreach (var stream in this.TakeStreams())
        {
            await stream.DisposeAsync().ConfigureAwait(false);
        }
    }

    private ImmutableArray<FileStream> TakeStreams()
    {
        lock (this.sync)
        {
            this.disposed = true;
            var owned = this.streams;
            this.streams = [];
            return owned;
        }
    }
}
