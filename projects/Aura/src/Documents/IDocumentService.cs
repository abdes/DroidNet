// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Application-facing document service contract. The app implements this interface
///     and raises events using <see cref="IDocumentMetadata"/>. Aura (the UI layer) subscribes
///     to the events to drive the TabStrip UI and maintain window document lists.
///
/// <para>
///     Threading and event contract:
/// </para>
/// <list type="bullet">
///     <item><description>Implementations may raise events from any thread. UI subscribers must marshal
///     to the UI thread (for example using <c>DispatcherQueue.TryEnqueue</c>) before touching UI controls.</description></item>
///     <item><description>The <see cref="DocumentClosing"/> event is a pre-close notification. Handlers may
///     register async veto tasks via <see cref="DocumentClosingEventArgs.AddVetoTask(Task{bool})"/>. The service
///     implementation should await all veto tasks and cancel the close if any veto returns <c>false</c>.</description></item>
///     <item><description>When the UI raises a close for a tab (TabStrip.TabCloseRequested), the UI should
///     call <see cref="CloseDocumentAsync(Guid,bool)"/>. Aura does not attempt to close windows - app is responsible
///     for deciding and implementing the policy to close windows or detach documents as needed.</description></item>
/// </list>
/// </summary>
public interface IDocumentService
{
    /// <summary>
    ///     Raised when a document is opened on a window.
    /// </summary>
    public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

    /// <summary>
    ///     Raised to allow application handlers to register async veto tasks before a document close.
    ///     Subscribers should use <see cref="DocumentClosingEventArgs.AddVetoTask(Task{bool})"/> if
    ///     they need to perform asynchronous checks that could veto the close.
    /// </summary>
    public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

    /// <summary>
    ///     Raised after a document closed successfully.
    /// </summary>
    public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

    /// <summary>
    ///     Raised when a document is detached from a window (e.g., tear-out started).
    /// </summary>
    public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

    /// <summary>
    ///     Raised when a document is attached to a window (e.g., on drop from tear-out).
    /// </summary>
    public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

    /// <summary>
    ///     Raised when app-provided metadata for a document changed (title, icon, dirty, etc).
    /// </summary>
    public event EventHandler<DocumentMetadataChangedEventArgs>? DocumentMetadataChanged;

    /// <summary>
    ///     Raised when the app activated a document in a window (selection).
    /// </summary>
    public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

    /// <summary>
    ///     Opens a document on the specified window. Returns the created document id
    ///     (which may equal <see cref="IDocumentMetadata.DocumentId"/> when the
    ///     application supplies it).
    /// </summary>
    /// <param name="window">The window to add the document to.</param>
    /// <param name="metadata">The document metadata supplied by the application.</param>
    /// <param name="indexHint">Preferred insertion index; -1 to append.</param>
    /// <param name="shouldSelect">When true the document will be selected after open.</param>
    /// <returns>The created document identifier.</returns>
    public Task<Guid> OpenDocumentAsync(WindowContext window, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true);

    /// <summary>
    ///     Requests close of the specified document. Returns true when the document
    ///     was successfully closed; false if the operation was vetoed by the app.
    /// </summary>
    /// <param name="documentId">The document identifier.</param>
    /// <param name="force">When true, bypasses app-level checks and forces closure.</param>
    /// <returns>True when the document closed; false if the app vetoed the close.</returns>
    public Task<bool> CloseDocumentAsync(Guid documentId, bool force = false);

    /// <summary>
    ///     Detaches a document from its host window as part of a tear-out flow.
    ///     Returns the document metadata when the detach succeeds or null if it fails.
    /// </summary>
    /// <param name="documentId">The document identifier to detach.</param>
    /// <returns>The document metadata when detached, or null on failure.</returns>
    public Task<IDocumentMetadata?> DetachDocumentAsync(Guid documentId);

    /// <summary>
    ///     Attaches an app-provided document to a target window.
    /// </summary>
    /// <param name="targetWindow">The window to attach the document to.</param>
    /// <param name="metadata">The provided document metadata.</param>
    /// <param name="indexHint">Preferred insertion index; -1 to append.</param>
    /// <param name="shouldSelect">When true the document will be selected after attach.</param>
    /// <returns>True when the operation succeeded.</returns>
    public Task<bool> AttachDocumentAsync(WindowContext targetWindow, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true);

    /// <summary>
    ///     Updates the metadata for an existing document.
    /// </summary>
    /// <param name="documentId">The id of the document to update.</param>
    /// <param name="metadata">The new metadata content.</param>
    /// <returns>True when the operation succeeded.</returns>
    public Task<bool> UpdateMetadataAsync(Guid documentId, IDocumentMetadata metadata);

    /// <summary>
    ///     Requests activation (selection) of the document in the specified window.
    /// </summary>
    /// <param name="window">The window that should host the document selection.</param>
    /// <param name="documentId">The id of the document to select.</param>
    /// <returns>True when the selection was successful.</returns>
    public Task<bool> SelectDocumentAsync(WindowContext window, Guid documentId);
}
