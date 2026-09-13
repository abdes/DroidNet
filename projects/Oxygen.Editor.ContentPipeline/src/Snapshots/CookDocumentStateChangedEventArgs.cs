// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Publishes one owner transition together with a consistent live-state snapshot.</summary>
/// <param name="before">The prior state, or null before its first update.</param>
/// <param name="after">The new state, or null after the owner closes.</param>
/// <param name="snapshot">The resulting state of all registered owners.</param>
public sealed class CookDocumentStateChangedEventArgs(CookDocumentState? before, CookDocumentState? after, CookDocumentRegistrySnapshot snapshot) : EventArgs
{
    /// <summary>Gets the prior state of this owner.</summary>
    public CookDocumentState? Before { get; } = before;

    /// <summary>Gets the new state of this owner.</summary>
    public CookDocumentState? After { get; } = after;

    /// <summary>Gets the consistent registry snapshot.</summary>
    public CookDocumentRegistrySnapshot Snapshot { get; } = snapshot;

    /// <summary>Gets a value indicating whether saved bytes changed, requiring a new freshness check.</summary>
    public bool SavedSourceChanged => this.Before is not null && this.After is not null
        && !string.Equals(this.Before.SavedContentHash, this.After.SavedContentHash, StringComparison.OrdinalIgnoreCase);
}
