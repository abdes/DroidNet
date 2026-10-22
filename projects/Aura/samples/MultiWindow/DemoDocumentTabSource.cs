// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using Microsoft.UI;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     The sample's documents, as the tabs of every window that shows them. A document has one tab, in one window: a
///     drag detaches it from its window and the window it is dropped on attaches it.
/// </summary>
/// <remarks>
///     Closing a tab asks the application first (<see cref="DocumentClosing"/>), unless forced. The source reports a
///     window whose last tab was closed by itself or moved to another window (<see cref="WindowEmptied"/>), so that the
///     sample closes an emptied document window. Every member runs on the UI thread.
/// </remarks>
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "The sample's view models take it")]
public sealed class DemoDocumentTabSource : IDocumentTabSource, IDocumentServiceState
{
    private readonly Dictionary<WindowId, Dictionary<DocumentId, IDocumentMetadata>> windowDocs = [];
    private readonly Dictionary<WindowId, DocumentId> activeDocuments = [];

    // The window each detached document left, until a window attaches it.
    private readonly Dictionary<DocumentId, WindowId> detachedFrom = [];

    /// <inheritdoc/>
    public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

    /// <inheritdoc/>
    public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

    /// <inheritdoc/>
    public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

    /// <inheritdoc/>
    public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

    /// <inheritdoc/>
    public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

    /// <inheritdoc/>
    public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

    /// <summary>
    ///     Raised when the last tab of a window has left it: the tab was closed by itself, or a drag moved it to another
    ///     window. Closing all of a window's documents, as its own close does, does not raise it.
    /// </summary>
    public event EventHandler<WindowEmptiedEventArgs>? WindowEmptied;

    /// <inheritdoc/>
    /// <remarks>A document has one tab: opening a tab for a document that has one, in any window, is refused.</remarks>
    public Task<bool> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        if (this.FindWindow(metadata.DocumentId) is not null)
        {
            return Task.FromResult(false);
        }

        this.Documents(windowId)[metadata.DocumentId] = metadata;
        Debug.WriteLine($"DemoDocumentTabSource: opened '{metadata.Title}' in window {windowId.Value}");
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, metadata, indexHint, shouldSelect));
        if (shouldSelect)
        {
            this.Activate(windowId, metadata.DocumentId);
        }

        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> CloseDocumentAsync(WindowId windowId, DocumentId documentId, bool force = false)
        => this.CloseAsync(windowId, documentId, force, emptiesWindow: true);

    /// <summary>
    ///     Closes the documents of a window that closes, asking about each one in that window.
    /// </summary>
    /// <param name="windowId">The window that closes.</param>
    /// <returns><see langword="true"/> when every document closed; <see langword="false"/> when the user kept one.</returns>
    public async Task<bool> CloseWindowDocumentsAsync(WindowId windowId)
    {
        foreach (var metadata in this.GetOpenDocuments(windowId))
        {
            if (!await this.CloseAsync(windowId, metadata.DocumentId, force: false, emptiesWindow: false).ConfigureAwait(true))
            {
                return false;
            }
        }

        return true;
    }

    /// <inheritdoc/>
    /// <remarks>The document stays open without a tab until a window attaches it.</remarks>
    public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, DocumentId documentId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.TryGetValue(documentId, out var metadata))
        {
            return Task.FromResult<IDocumentMetadata?>(null);
        }

        this.RemoveTab(windowId, documentId);
        this.detachedFrom[documentId] = windowId;
        Debug.WriteLine($"DemoDocumentTabSource: detached '{metadata.Title}' from window {windowId.Value}");
        this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
        return Task.FromResult<IDocumentMetadata?>(metadata);
    }

    /// <inheritdoc/>
    /// <remarks>
    ///     A document has one tab: attaching a document that has one, in any window, is refused. The window has the tab
    ///     before it is selected.
    /// </remarks>
    public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        if (this.FindWindow(metadata.DocumentId) is not null)
        {
            return Task.FromResult(false);
        }

        this.Documents(targetWindowId)[metadata.DocumentId] = metadata;
        Debug.WriteLine($"DemoDocumentTabSource: attached '{metadata.Title}' to window {targetWindowId.Value}");
        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        if (shouldSelect)
        {
            this.Activate(targetWindowId, metadata.DocumentId);
        }

        // A drag that moved the last tab of its window leaves that window empty.
        if (this.detachedFrom.Remove(metadata.DocumentId, out var source) && source != targetWindowId
            && !this.windowDocs.ContainsKey(source))
        {
            this.WindowEmptied?.Invoke(this, new WindowEmptiedEventArgs(source));
        }

        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> SelectDocumentAsync(WindowId windowId, DocumentId documentId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.ContainsKey(documentId))
        {
            return Task.FromResult(false);
        }

        this.Activate(windowId, documentId);
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public IReadOnlyList<IDocumentMetadata> GetOpenDocuments(WindowId windowId)
        => this.windowDocs.TryGetValue(windowId, out var docs) ? [.. docs.Values] : [];

    /// <inheritdoc/>
    public DocumentId? GetActiveDocumentId(WindowId windowId)
        => this.activeDocuments.TryGetValue(windowId, out var documentId) ? documentId : null;

    private async Task<bool> CloseAsync(WindowId windowId, DocumentId documentId, bool force, bool emptiesWindow)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.TryGetValue(documentId, out var metadata))
        {
            return false;
        }

        // The application may refuse a close that is not forced, such as by asking the user.
        var closing = new DocumentClosingEventArgs(windowId, metadata, force);
        this.DocumentClosing?.Invoke(this, closing);
        if (!force && !await closing.WaitForVetoResultAsync().ConfigureAwait(true))
        {
            Debug.WriteLine($"DemoDocumentTabSource: the close of '{metadata.Title}' was refused");
            return false;
        }

        // The answer may have taken a while: the tab may have closed or moved meanwhile.
        if (!this.windowDocs.TryGetValue(windowId, out docs) || !docs.ContainsKey(documentId))
        {
            return false;
        }

        this.RemoveTab(windowId, documentId);
        Debug.WriteLine($"DemoDocumentTabSource: closed '{metadata.Title}' in window {windowId.Value}");
        this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(windowId, metadata));
        if (emptiesWindow && !this.windowDocs.ContainsKey(windowId))
        {
            this.WindowEmptied?.Invoke(this, new WindowEmptiedEventArgs(windowId));
        }

        return true;
    }

    private void Activate(WindowId windowId, DocumentId documentId)
    {
        this.activeDocuments[windowId] = documentId;
        this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
    }

    private WindowId? FindWindow(DocumentId documentId)
        => this.windowDocs.Where(entry => entry.Value.ContainsKey(documentId)).Select(static entry => (WindowId?)entry.Key).FirstOrDefault();

    private void RemoveTab(WindowId windowId, DocumentId documentId)
    {
        var docs = this.windowDocs[windowId];
        _ = docs.Remove(documentId);
        if (docs.Count == 0)
        {
            _ = this.windowDocs.Remove(windowId);
        }

        if (this.activeDocuments.TryGetValue(windowId, out var active) && active == documentId)
        {
            _ = this.activeDocuments.Remove(windowId);
        }
    }

    private Dictionary<DocumentId, IDocumentMetadata> Documents(WindowId windowId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs))
        {
            docs = [];
            this.windowDocs[windowId] = docs;
        }

        return docs;
    }
}
