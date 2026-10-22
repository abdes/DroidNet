// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using DroidNet.Documents;
using Microsoft.UI;

namespace DroidNet.Aura.Documents;

/// <summary>
///     The documents a tab drag is moving between windows: each one from the moment its window's tabs detached it until
///     the tabs of the window it is dropped on, or of the window it came from, attach it again.
/// </summary>
/// <remarks>
///     <para>
///     The <see cref="DocumentTabPresenter" /> of every window shares the application's transfers: the presenter whose
///     strip a tab is dragged out of holds the detached document here, and the presenter whose strip the tab is dropped
///     on takes it. A tab dropped outside every strip goes to a window that <see cref="IDocumentWindowFactory" />
///     creates; without one, or when that window is not created, the document goes back where it came from, and so
///     does one dropped on a strip whose tabs come from another source.
///     </para>
///     <para>
///     Registered with the drag services (<see cref="AuraOptions.WithDrag" />).
///     </para>
/// </remarks>
/// <param name="windows">Creates the window of a torn-out document, if the application creates them.</param>
public sealed class DocumentTransfers(IDocumentWindowFactory? windows = null)
{
    private readonly Lock gate = new();
    private readonly Dictionary<DocumentId, DocumentTransfer> moving = [];

    /// <summary>Holds a document that a window's tabs detached for a drag.</summary>
    /// <param name="document">The detached document.</param>
    /// <param name="source">The window it was detached from.</param>
    /// <param name="tabs">The source of that window's tabs, which detached it.</param>
    internal void Hold(IDocumentMetadata document, WindowId source, IDocumentTabSource tabs)
    {
        lock (this.gate)
        {
            this.moving[document.DocumentId] = new DocumentTransfer(document, source, tabs);
        }
    }

    /// <summary>Takes a document a drag is moving, so that only one window attaches it.</summary>
    /// <param name="documentId">The document.</param>
    /// <returns>The document and where it came from, or <see langword="null" /> when no drag is moving it.</returns>
    internal DocumentTransfer? Take(DocumentId documentId)
    {
        lock (this.gate)
        {
            return this.moving.Remove(documentId, out var transfer) ? transfer : null;
        }
    }

    /// <summary>Creates the window of a torn-out document.</summary>
    /// <param name="document">The document.</param>
    /// <param name="position">Where its tab was dropped, in physical screen pixels.</param>
    /// <returns>The window, or <see langword="null" /> when the application creates none.</returns>
    internal Task<WindowId?> CreateWindowAsync(IDocumentMetadata document, SpatialPoint<PhysicalScreenSpace> position)
        => windows?.CreateWindowAsync(document, position) ?? Task.FromResult<WindowId?>(null);
}
