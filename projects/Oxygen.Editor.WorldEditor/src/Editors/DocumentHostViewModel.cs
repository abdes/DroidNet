// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Documents;
using DroidNet.Mvvm;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Documents;
using Oxygen.Editor.WorldEditor.Editors.Scene;
using Oxygen.Editor.WorldEditor.Engine;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// The ViewModel for the <see cref="Oxygen.Editor.WorldEditor.Editors.DocumentHostView"/> view.
/// Orchestrates the interaction between the <see cref="IDocumentService"/> and the editor content.
/// </summary>
public partial class DocumentHostViewModel : ObservableObject, IDisposable // TODO: refactor to use IAsyncDisposable
{
    private readonly ILogger logger;
    private readonly ILoggerFactory? loggerFactory;
    private readonly IEngineService engineService;
    private readonly IContainer container;

    private readonly IViewLocator viewLocator;
    private readonly Dictionary<Guid, object> activeEditors = [];

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentHostViewModel" /> class.
    /// </summary>
    /// <param name="documentService">The document service used to manage documents.</param>
    /// <param name="viewLocator">The view locator used to resolve views for editor view models.</param>
    /// <param name="engineService">Coordinates the shared engine lifecycle.</param>
    /// <param name="container">The dependency injection container used to resolve services and manage editor lifetimes.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
    ///     process. If <see langword="null" />, logging is disabled.
    /// </param>
    public DocumentHostViewModel(
        IDocumentService documentService,
        IViewLocator viewLocator,
        IEngineService engineService,
        IContainer container,
        ILoggerFactory? loggerFactory = null)
    {
        this.loggerFactory = loggerFactory;
        this.logger = loggerFactory?.CreateLogger<DocumentHostViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentHostViewModel>();

        this.container = container;
        this.DocumentService = documentService;
        this.viewLocator = viewLocator;
        this.engineService = engineService;

        this.DocumentService.DocumentOpened += this.OnDocumentOpened;
        this.DocumentService.DocumentClosed += this.OnDocumentClosed;
        this.DocumentService.DocumentActivated += this.OnDocumentActivated;

        this.LogInitialized();
    }

    [ObservableProperty]
    public partial object? ActiveEditor { get; set; }

    /// <summary>
    /// Gets the document service used to manage documents.
    /// </summary>
    public IDocumentService DocumentService { get; }

    [ObservableProperty]
    public partial object? ActiveEditorView { get; private set; }

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
            this.DocumentService.DocumentOpened -= this.OnDocumentOpened;
            this.DocumentService.DocumentClosed -= this.OnDocumentClosed;
            this.DocumentService.DocumentActivated -= this.OnDocumentActivated;

            foreach (var documentId in this.activeEditors.Keys.ToArray())
            {
                _ = this.ReleaseDocumentSurfacesAsync(documentId);
            }
        }
    }

    private void OnDocumentOpened(object? sender, DocumentOpenedEventArgs e)
    {
        this.LogOnDocumentOpened(e.Metadata.DocumentId, e.Metadata.GetType().Name);

        // Create the editor ViewModel based on metadata type
        object? editor = null;
        if (e.Metadata is SceneDocumentMetadata sceneMeta)
        {
            editor = new SceneEditorViewModel(sceneMeta, this.engineService, this.container, this.loggerFactory);
        }
        else
        {
            this.LogUnknownMetadataType(e.Metadata.GetType().Name);
        }

        if (editor != null)
        {
            this.activeEditors[e.Metadata.DocumentId] = editor;
            if (e.ShouldSelect)
            {
                this.LogSelectingEditor(e.Metadata.DocumentId);
                this.ActiveEditor = editor;
            }
        }
        else
        {
            this.LogFailedToCreateEditor(e.Metadata.DocumentId);
        }
    }

    private void OnDocumentClosed(object? sender, DocumentClosedEventArgs e)
    {
        this.LogOnDocumentClosed(e.Metadata.DocumentId);
        if (this.activeEditors.TryGetValue(e.Metadata.DocumentId, out var editor))
        {
            _ = this.activeEditors.Remove(e.Metadata.DocumentId);
            if (ReferenceEquals(this.ActiveEditor, editor))
            {
                // Log that ActiveEditor is being cleared for this document
                this.ActiveEditor = null;
            }

            if (editor is IDisposable disposable)
            {
                disposable.Dispose();
                this.LogEditorDisposed(e.Metadata.DocumentId);
            }

            _ = this.ReleaseDocumentSurfacesAsync(e.Metadata.DocumentId);
        }
    }

    private void OnDocumentActivated(object? sender, DocumentActivatedEventArgs e)
    {
        this.LogOnDocumentActivated(e.DocumentId);

        if (this.activeEditors.TryGetValue(e.DocumentId, out var editor))
        {
            // Log that ActiveEditor is being set due to activation
            this.ActiveEditor = editor;
        }
        else
        {
            this.LogOnDocumentActivatedEditorNotFound(e.DocumentId);
        }
    }

    private async Task ReleaseDocumentSurfacesAsync(Guid documentId)
        => await this.engineService.ReleaseDocumentSurfacesAsync(documentId).ConfigureAwait(true);

    partial void OnActiveEditorChanged(object? value)
    {
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

        this.ActiveEditorView = view;
    }
}
