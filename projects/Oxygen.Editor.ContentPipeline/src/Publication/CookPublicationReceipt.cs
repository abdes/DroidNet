// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>The committed generation, source provenance, and native-validated output identities.</summary>
/// <param name="Version">The receipt format.</param>
/// <param name="ProjectId">The owning project.</param>
/// <param name="OperationId">The publication journal identity.</param>
/// <param name="CompletedAt">When the generation was prepared for commit.</param>
/// <param name="BuildFingerprint">The producer/schema identity.</param>
/// <param name="InputIdentity">The saved input set fingerprint.</param>
/// <param name="Inputs">Saved source paths and byte identities.</param>
/// <param name="Documents">Captured saved document revisions.</param>
/// <param name="Roots">The known validated root set, including preserved products.</param>
/// <param name="WasMounted">Whether a live preview participated in publication.</param>
internal sealed record CookPublicationReceipt(
    int Version,
    Guid ProjectId,
    Guid OperationId,
    DateTimeOffset CompletedAt,
    string BuildFingerprint,
    string InputIdentity,
    ImmutableArray<CookSnapshotInput> Inputs,
    ImmutableArray<CookDocumentState> Documents,
    ImmutableArray<CookProvenance.Root> Roots,
    bool WasMounted)
{
    /// <summary>Gets the selected foreign cooked inputs consumed by this generation.</summary>
    public ImmutableArray<CookedDependencySnapshot> CookedDependencies { get; init; } = [];
}
