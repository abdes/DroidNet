// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Acquires document read leases in stable order without holding registry locks across callbacks.</summary>
/// <param name="logger">Presentation observer diagnostics.</param>
public sealed partial class CookDocumentRegistry(ILogger<CookDocumentRegistry>? logger = null) : ICookDocumentRegistry
{
    private readonly Lock sync = new();
    private readonly Dictionary<long, Registration> registrations = [];
    private readonly ILogger<CookDocumentRegistry> logger = logger ?? NullLogger<CookDocumentRegistry>.Instance;
    private long nextId;
    private long stateVersion;

    /// <inheritdoc />
    public event EventHandler<CookDocumentStateChangedEventArgs>? StateChanged;

    /// <inheritdoc />
    public CookDocumentRegistrySnapshot GetState()
    {
        lock (this.sync)
        {
            return this.CaptureState();
        }
    }

    /// <inheritdoc />
    public ICookDocumentRegistration Register(string sourcePath, Func<CancellationToken, Task<CookDocumentReadLease?>> acquire)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        ArgumentNullException.ThrowIfNull(acquire);
        lock (this.sync)
        {
            var registration = new Registration(this, ++this.nextId, Path.GetFullPath(sourcePath), acquire);
            this.registrations.Add(registration.Id, registration);
            return registration;
        }
    }

    /// <inheritdoc />
    public async Task<CookDocumentReadSet> AcquireAsync(IEnumerable<string> sourcePaths, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sourcePaths);
        cancellationToken.ThrowIfCancellationRequested();
        var paths = sourcePaths.Select(Path.GetFullPath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        (Registration owner, string sourcePath)[] selected;
        lock (this.sync)
        {
            selected = this.registrations.Values
                .Where(entry => paths.Contains(entry.SourcePath))
                .OrderBy(static entry => entry.SourcePath, StringComparer.OrdinalIgnoreCase)
                .ThenBy(static entry => entry.Id)
                .Select(static entry => (entry, entry.SourcePath))
                .ToArray();
        }

        var leases = new List<CookDocumentReadLease>();
        try
        {
            foreach (var (owner, sourcePath) in selected)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (await owner.Acquire(cancellationToken).ConfigureAwait(false) is { } lease)
                {
                    leases.Add(lease);
                    if (!SourcePathsMatch(lease.State.SourcePath, sourcePath))
                    {
                        throw new OperationCanceledException("The document source moved while cook capture was waiting for it.", new CancellationToken(canceled: true));
                    }
                }
            }

            return new CookDocumentReadSet(leases);
        }
        catch
        {
            for (var index = leases.Count - 1; index >= 0; index--)
            {
                leases[index].Dispose();
            }

            throw;
        }
    }

    // Registrations and owner states are stored canonically, but a lease callback may report the
    // same source with different separator spelling; only a genuinely different path is a move.
    private static bool SourcePathsMatch(string stateSourcePath, string sourcePath)
    {
        try
        {
            return string.Equals(Path.GetFullPath(stateSourcePath), sourcePath, StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception invalid) when (invalid is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }

    private void Remove(long id)
    {
        CookDocumentStateChangedEventArgs? change = null;
        lock (this.sync)
        {
            var before = this.registrations.GetValueOrDefault(id)?.State;
            if (this.registrations.Remove(id) && before is not null)
            {
                this.stateVersion++;
                change = new(before, after: null, this.CaptureState());
            }
        }

        if (change is not null)
        {
            this.PublishState(change);
        }
    }

    private sealed partial class Registration(
        CookDocumentRegistry owner,
        long id,
        string sourcePath,
        Func<CancellationToken, Task<CookDocumentReadLease?>> acquire) : ICookDocumentRegistration
    {
        private CookDocumentRegistry? owner = owner;

        public long Id { get; } = id;

        public string SourcePath { get; set; } = sourcePath;

        public Func<CancellationToken, Task<CookDocumentReadLease?>> Acquire { get; } = acquire;

        public CookDocumentState? State { get; set; }

        public void UpdateState(CookDocumentState state) => Volatile.Read(ref this.owner)?.UpdateState(this.Id, state);

        public void RelocateSource(CookDocumentState state) => Volatile.Read(ref this.owner)?.UpdateState(this.Id, state, relocate: true);

        public void Dispose() => Interlocked.Exchange(ref this.owner, value: null)?.Remove(this.Id);
    }
}
