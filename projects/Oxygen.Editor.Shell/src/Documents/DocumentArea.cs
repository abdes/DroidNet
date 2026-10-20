// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The document area of one window, over the tabs of <see cref="IDocumentTabs"/> in that window.
/// </summary>
/// <remarks>
///     Every document of <see cref="OpenDocuments"/> that gets a tab here gets a <see cref="DocumentTab"/>. When that tab
///     closes, by the user or by <see cref="CloseAsync"/>, the area disposes the tab and closes the document.
/// </remarks>
public sealed partial class DocumentArea : IDocumentArea, IDisposable
{
    private readonly IDocumentTabs tabs;
    private readonly OpenDocuments documents;
    private readonly WindowId window;
    private readonly Action<WindowId> bringForward;
    private readonly ILogger logger;
    private readonly Dictionary<IDocumentKind, DocumentEditorDescriptor> editors;
    private readonly List<DocumentTab> shown = [];
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentArea"/> class.
    /// </summary>
    /// <param name="tabs">The service that holds the tabs of every window.</param>
    /// <param name="documents">The open documents of the session.</param>
    /// <param name="editors">The editor descriptors, one for each kind of <paramref name="documents"/>.</param>
    /// <param name="window">The window whose document area this is.</param>
    /// <param name="bringForward">Brings a window to the foreground.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    /// <exception cref="InvalidOperationException">
    ///     A kind of <paramref name="documents"/> has no descriptor, or a descriptor names a kind that
    ///     <paramref name="documents"/> does not open.
    /// </exception>
    public DocumentArea(
        IDocumentTabs tabs,
        OpenDocuments documents,
        IEnumerable<DocumentEditorDescriptor> editors,
        WindowId window,
        Action<WindowId> bringForward,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(tabs);
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(editors);
        ArgumentNullException.ThrowIfNull(bringForward);
        this.tabs = tabs;
        this.documents = documents;
        this.window = window;
        this.bringForward = bringForward;
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<DocumentArea>();
        this.editors = Validate(documents.Kinds, editors);

        this.tabs.DocumentOpened += this.OnTabOpened;
        this.tabs.DocumentActivated += this.OnTabActivated;
        this.tabs.DocumentClosed += this.OnTabClosed;
    }

    /// <inheritdoc/>
    public event EventHandler? ActiveDocumentChanged;

    /// <summary>
    ///     Raised when a document's tab is added to this area, before it is selected.
    /// </summary>
    internal event EventHandler<DocumentTabEventArgs>? TabAdded;

    /// <summary>
    ///     Raised when a document's tab leaves this area, before the document closes.
    /// </summary>
    internal event EventHandler<DocumentTabEventArgs>? TabRemoved;

    /// <inheritdoc/>
    public IReadOnlyList<Document> Documents => [.. this.shown.Select(static tab => tab.Document)];

    /// <inheritdoc/>
    public Document? ActiveDocument { get; private set; }

    /// <summary>
    ///     Gets the window whose document area this is.
    /// </summary>
    internal WindowId Window => this.window;

    /// <inheritdoc/>
    public async Task<bool> ShowAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (!this.documents.Documents.Contains(document))
        {
            throw new ArgumentException($"'{document.Title}' is not open.", nameof(document));
        }

        // A document has one tab: when another window has it, that window shows it.
        if (this.tabs.FindWindow(document.Id) is { } owner)
        {
            return await this.SelectAsync(owner, document).ConfigureAwait(true);
        }

        var tab = new DocumentTab(document, this.editors[document.Kind]);
        this.shown.Add(tab);
        if (await this.tabs.OpenDocumentAsync(this.window, tab).ConfigureAwait(true))
        {
            this.bringForward(this.window);
            return true;
        }

        this.LogShowRefused(document.Title);
        _ = this.shown.Remove(tab);
        tab.Dispose();
        return false;
    }

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseAsync(IReadOnlyList<Document> documents)
    {
        ArgumentNullException.ThrowIfNull(documents);
        return this.tabs.PrepareCloseDocumentsAsync(this.window, [.. documents.Select(static document => document.Id)]);
    }

    /// <inheritdoc/>
    public Task<bool> CloseAsync(Document document, bool force = false)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.tabs.CloseDocumentAsync(this.window, document.Id, force);
    }

    /// <summary>
    ///     Stops following the window's tabs. The documents stay open.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.tabs.DocumentOpened -= this.OnTabOpened;
        this.tabs.DocumentActivated -= this.OnTabActivated;
        this.tabs.DocumentClosed -= this.OnTabClosed;
        foreach (var tab in this.shown)
        {
            tab.Dispose();
        }

        this.shown.Clear();
    }

    /// <summary>
    ///     Gets the editor descriptor of a kind that documents of this area open with.
    /// </summary>
    /// <param name="kind">The kind.</param>
    /// <returns>The kind's descriptor.</returns>
    internal DocumentEditorDescriptor EditorOf(IDocumentKind kind) => this.editors[kind];

    /// <summary>
    ///     Gets the tab of a document shown in this area.
    /// </summary>
    /// <param name="document">The document.</param>
    /// <returns>The tab, or <see langword="null"/> when the document has no tab here.</returns>
    internal DocumentTab? TabOf(Document document) => this.Find(document.Id);

    private static Dictionary<IDocumentKind, DocumentEditorDescriptor> Validate(
        IReadOnlyList<IDocumentKind> kinds,
        IEnumerable<DocumentEditorDescriptor> editors)
    {
        var byKind = new Dictionary<IDocumentKind, DocumentEditorDescriptor>(ReferenceEqualityComparer.Instance);
        foreach (var editor in editors)
        {
            if (!kinds.Contains(editor.Kind))
            {
                throw new InvalidOperationException($"The {editor.Kind.Name} editor names a kind that no document opens with.");
            }

            if (!byKind.TryAdd(editor.Kind, editor))
            {
                throw new InvalidOperationException($"The {editor.Kind.Name} kind has two editors.");
            }
        }

        return kinds.FirstOrDefault(kind => !byKind.ContainsKey(kind)) is { } missing
            ? throw new InvalidOperationException($"The {missing.Name} kind has no editor.")
            : byKind;
    }

    private DocumentTab? Find(DocumentId id) => this.shown.FirstOrDefault(tab => tab.DocumentId == id);

    private async Task<bool> SelectAsync(WindowId owner, Document document)
    {
        if (!await this.tabs.SelectDocumentAsync(owner, document.Id).ConfigureAwait(true))
        {
            this.LogShowRefused(document.Title);
            return false;
        }

        this.bringForward(owner);
        return true;
    }

    private void OnTabOpened(object? sender, DocumentOpenedEventArgs args)
    {
        if (args.WindowId == this.window && args.Metadata is DocumentTab tab && this.shown.Contains(tab))
        {
            this.TabAdded?.Invoke(this, new DocumentTabEventArgs(tab));
        }
    }

    private void OnTabActivated(object? sender, DocumentActivatedEventArgs args)
    {
        if (args.WindowId == this.window)
        {
            // A tab that is not a document's, such as a legacy editor's, leaves no document active.
            this.SetActive(this.Find(args.DocumentId)?.Document);
        }
    }

    private void OnTabClosed(object? sender, DocumentClosedEventArgs args)
    {
        if (args.WindowId != this.window || args.Metadata is not DocumentTab tab || !this.shown.Remove(tab))
        {
            return;
        }

        if (ReferenceEquals(this.ActiveDocument, tab.Document))
        {
            this.SetActive(document: null);
        }

        this.TabRemoved?.Invoke(this, new DocumentTabEventArgs(tab));
        tab.Dispose();

        this.LogDocumentClosed(tab.Document.Kind.Name, tab.Document.Title, tab.Document.IsDirty);
        this.documents.Close(tab.Document);
    }

    private void SetActive(Document? document)
    {
        if (ReferenceEquals(this.ActiveDocument, document))
        {
            return;
        }

        this.ActiveDocument = document;
        this.ActiveDocumentChanged?.Invoke(this, EventArgs.Empty);
    }
}
