// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Holds a document's existing save gate while its saved input is copied.</summary>
/// <param name="state">Document state captured by its authoring owner.</param>
/// <param name="release">Releases the owner's acquired save gate.</param>
public sealed partial class CookDocumentReadLease(CookDocumentState state, Action release) : IDisposable
{
    private Action? release = release;

    /// <summary>Gets the immutable state protected by this lease.</summary>
    public CookDocumentState State { get; } = state;

    /// <inheritdoc />
    public void Dispose() => Interlocked.Exchange(ref this.release, value: null)?.Invoke();
}
