// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Owns the document leases for one source-input capture.</summary>
public sealed partial class CookDocumentReadSet : IDisposable
{
    private IReadOnlyList<CookDocumentReadLease>? leases;

    /// <summary>Initializes a new instance of the <see cref="CookDocumentReadSet"/> class.</summary>
    /// <param name="leases">The acquired save-gate leases transferred to this owner.</param>
    internal CookDocumentReadSet(IReadOnlyList<CookDocumentReadLease> leases)
    {
        this.leases = leases;
        this.Documents = leases.Select(static lease => lease.State).ToImmutableArray();
    }

    /// <summary>Gets the captured facts for every participating open document.</summary>
    public ImmutableArray<CookDocumentState> Documents { get; }

    /// <inheritdoc />
    public void Dispose()
    {
        var captured = Interlocked.Exchange(ref this.leases, value: null);
        if (captured is null)
        {
            return;
        }

        for (var index = captured.Count - 1; index >= 0; index--)
        {
            captured[index].Dispose();
        }
    }
}
