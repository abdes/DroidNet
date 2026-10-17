// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using DroidNet.Mvvm;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.SceneEditor;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// The ViewModel for the <see cref="Oxygen.Editor.World.Documents.DocumentHostView"/> view.
/// It shows the editor of the active tab of its window, and creates each tab's editor from the tab's descriptor.
/// </summary>
public partial class DocumentHostViewModel : ObservableObject, IDisposable // TODO: refactor to use IAsyncDisposable
{
    private readonly ILogger logger;
    private readonly IEngineService engineService;
    private readonly IEditorDocumentService tabs;
    private readonly IDocumentArea documentArea;
    private readonly OpenDocuments documents;
    private readonly WindowId windowId;
    private readonly DocumentCloseCoordinator closeCoordinator;
    private readonly WorkspaceCloseCoordinator workspaceCloseCoordinator;

    private readonly IViewLocator viewLocator;
    private readonly Dictionary<DocumentId, object> activeEditors = [];
    private readonly SemaphoreSlim activeEditorViewGate = new(initialCount: 1, maxCount: 1);
    private long activeEditorViewRequestId;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentHostViewModel" /> class.
    /// </summary>
    /// <param name="tabs">The service that holds the tabs of every window.</param>
    /// <param name="documentArea">The document area of the host's window.</param>
    /// <param name="documents">The open documents, by which saves and recovery actions find a document.</param>
    /// <param name="viewLocator">The view locator used to resolve views for editor view models.</param>
    /// <param name="engineService">Coordinates the shared engine lifecycle.</param>
    /// <param name="closeCoordinator">The shared authoring close coordinator.</param>
    /// <param name="windowManager">The window lifecycle service.</param>
    /// <param name="windowId">The window identifier.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
    ///     process. If <see langword="null" />, logging is disabled.
    /// </param>
    public DocumentHostViewModel(
        IEditorDocumentService tabs,
        IDocumentArea documentArea,
        OpenDocuments documents,
        IViewLocator viewLocator,
        IEngineService engineService,
        DocumentCloseCoordinator closeCoordinator,
        IWindowManagerService windowManager,
        WindowId windowId,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentHostViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentHostViewModel>();

        this.tabs = tabs;
        this.documentArea = documentArea;
        this.documents = documents;
        this.viewLocator = viewLocator;
        this.engineService = engineService;
        this.windowId = windowId;
        this.closeCoordinator = closeCoordinator;
        this.workspaceCloseCoordinator = new WorkspaceCloseCoordinator(windowManager, tabs, windowId);

        this.tabs.DocumentOpened += this.OnDocumentOpened;
        this.tabs.DocumentClosed += this.OnDocumentClosed;
        this.tabs.DocumentActivated += this.OnDocumentActivated;
        this.documentArea.ActiveDocumentChanged += this.OnActiveDocumentChanged;

        this.LogInitialized();
    }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveActiveDocumentCommand))]
    public partial object? ActiveEditor { get; set; }

    [ObservableProperty]
    public partial object? ActiveEditorView { get; private set; }

    /// <summary>Saves the open documents of an explicit list of assets through their editors.</summary>
    /// <param name="assets">The assets whose open documents the user authorized saving.</param>
    /// <returns>Whether no listed open document remains unsaved.</returns>
    public Task<bool> SaveDocumentsAsync(IReadOnlyList<Uri> assets)
        => this.SaveAsync([.. assets.Select(this.documents.FindByAsset).OfType<Document>()]);

    /// <summary>Saves every document of the window with unsaved changes, through their editors.</summary>
    /// <returns>Whether no document of the window remains unsaved.</returns>
    public Task<bool> SaveAllAsync()
        => this.SaveAsync([.. this.documentArea.Documents.Where(static document => document.IsDirty)]);

    /// <summary>Shows the open document of an asset for an explicit recovery action.</summary>
    /// <param name="asset">The asset whose open document is shown.</param>
    /// <returns>Whether the document was shown.</returns>
    public Task<bool> SelectDocumentAsync(Uri asset)
        => this.documents.FindByAsset(asset) is { } document ? this.documentArea.ShowAsync(document) : Task.FromResult(false);

    /// <summary>Shows an open document.</summary>
    /// <param name="document">The id of the document to show.</param>
    /// <returns>Whether the document was shown.</returns>
    public Task<bool> SelectDocumentAsync(DocumentId document)
        => this.documents.Find(document) is { } open ? this.documentArea.ShowAsync(open) : Task.FromResult(false);

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases unmanaged and - optionally - managed resources.
    /// </summary>
    /// <param name="disposing"><see langword="true"/> to release both managed and unmanaged resources; <see langword="false"/> to release only unmanaged resources.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.LogDisposing();
            this.workspaceCloseCoordinator.Dispose();
            this.tabs.DocumentOpened -= this.OnDocumentOpened;
            this.tabs.DocumentClosed -= this.OnDocumentClosed;
            this.tabs.DocumentActivated -= this.OnDocumentActivated;
            this.documentArea.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
            this.ObserveActiveDocument(null);

            foreach (var documentId in this.activeEditors.Keys.ToArray())
            {
                this.closeCoordinator.Unregister(this.windowId, documentId);
                if (this.activeEditors[documentId] is IDisposable disposable)
                {
                    disposable.Dispose();
                }

                _ = this.ReleaseDocumentSurfacesAsync(documentId);
            }

            this.activeEditorViewGate.Dispose();
        }
    }

    // Each editor saves its own document, so its input commit, conflict handling and prompts run.
    private async Task<bool> SaveAsync(IReadOnlyList<Document> documents)
    {
        foreach (var document in documents)
        {
            if (this.activeEditors.TryGetValue(document.Id, out var editor) && editor is IAsyncSaveable saveable)
            {
                await saveable.SaveAsync().ConfigureAwait(true);
                if (document.IsDirty)
                {
                    return false;
                }
            }
        }

        return true;
    }

    private void OnDocumentOpened(object? sender, DocumentOpenedEventArgs e)
    {
        if (e.WindowId != this.windowId)
        {
            return;
        }

        this.LogOnDocumentOpened(e.Metadata.DocumentId, e.Metadata.GetType().Name);
        if (e.Metadata is not DocumentTab tab)
        {
            this.LogUnknownMetadataType(e.Metadata.GetType().Name);
            return;
        }

        if (tab.Editor.CreateEditor(tab) is not IDocumentCloseParticipant editor)
        {
            this.LogFailedToCreateEditor(tab.DocumentId);
            return;
        }

        this.activeEditors[tab.DocumentId] = editor;
        this.closeCoordinator.Register(this.windowId, tab.DocumentId, editor);
        if (e.ShouldSelect)
        {
            this.LogSelectingEditor(tab.DocumentId);
            this.ActiveEditor = editor;
        }
    }

    private void OnDocumentClosed(object? sender, DocumentClosedEventArgs e)
    {
        if (e.WindowId != this.windowId)
        {
            return;
        }

        var documentId = e.Metadata.DocumentId;
        this.closeCoordinator.Unregister(this.windowId, documentId);
        this.LogOnDocumentClosed(documentId);
        if (this.activeEditors.Remove(documentId, out var editor))
        {
            if (ReferenceEquals(this.ActiveEditor, editor))
            {
                this.ActiveEditor = null;
            }

            if (editor is IDisposable disposable)
            {
                disposable.Dispose();
                this.LogEditorDisposed(documentId);
            }

            _ = this.ReleaseDocumentSurfacesAsync(documentId);
        }
    }

    private void OnDocumentActivated(object? sender, DocumentActivatedEventArgs e)
    {
        if (e.WindowId != this.windowId)
        {
            return;
        }

        this.LogOnDocumentActivated(e.DocumentId);
        if (this.activeEditors.TryGetValue(e.DocumentId, out var editor))
        {
            this.ActiveEditor = editor;
            this.SaveActiveDocumentCommand.NotifyCanExecuteChanged();
        }
        else
        {
            this.LogOnDocumentActivatedEditorNotFound(e.DocumentId);
        }
    }

    private async Task ReleaseDocumentSurfacesAsync(DocumentId documentId)
        => await this.engineService.ReleaseOwnerSurfacesAsync(new SurfaceOwnerId(documentId.Value)).ConfigureAwait(true);

    [RelayCommand(CanExecute = nameof(CanSaveActiveDocument))]
    private async Task SaveActiveDocument()
    {
        if (this.ActiveEditor is IAsyncSaveable saveable)
        {
            await saveable.SaveAsync().ConfigureAwait(true);
        }
    }

    private bool CanSaveActiveDocument() => this.ActiveEditor is IAsyncSaveable;

    partial void OnActiveEditorChanged(object? value)
    {
        var requestId = Interlocked.Increment(ref this.activeEditorViewRequestId);
        _ = this.SwitchActiveEditorViewAsync(value, requestId);
    }

    partial void OnActiveEditorChanging(object? oldValue, object? newValue)
    {
        if (!ReferenceEquals(oldValue, newValue) && oldValue is MaterialEditorViewModel material)
        {
            material.Deactivate();
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "View teardown failures are logged so an approved document transition can finish without crashing the UI.")]
    private async Task SwitchActiveEditorViewAsync(object? value, long requestId)
    {
        await this.activeEditorViewGate.WaitAsync().ConfigureAwait(true);
        try
        {
            if (requestId != Volatile.Read(ref this.activeEditorViewRequestId) || !ReferenceEquals(value, this.ActiveEditor))
            {
                return;
            }

            if (this.ActiveEditorView is SceneEditorView previousSceneView)
            {
                try
                {
                    await previousSceneView.DeactivateAsync().ConfigureAwait(true);
                }
                catch (Exception ex)
                {
                    this.LogViewDeactivationFailed(ex);
                }
            }

            if (requestId != Volatile.Read(ref this.activeEditorViewRequestId) || !ReferenceEquals(value, this.ActiveEditor))
            {
                return;
            }

            if (value is null)
            {
                this.ActiveEditorView = null;
                return;
            }

            var view = this.viewLocator.ResolveView(value);

            if (view is DroidNet.Mvvm.IViewFor nonGeneric)
            {
                nonGeneric.ViewModel = value;
            }

            if (requestId != Volatile.Read(ref this.activeEditorViewRequestId) || !ReferenceEquals(value, this.ActiveEditor))
            {
                if (view is SceneEditorView staleSceneView)
                {
                    await staleSceneView.DeactivateAsync().ConfigureAwait(true);
                }

                return;
            }

            this.ActiveEditorView = view;
        }
        finally
        {
            _ = this.activeEditorViewGate.Release();
        }
    }
}
