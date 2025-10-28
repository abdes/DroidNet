// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// The ViewModel for the <see cref="Oxygen.Editor.WorldEditor.Views.TabbedDocumentView"/> view.
/// Implements a simple document host with windowing support.
/// </summary>
public partial class DocumentHostViewModel : ObservableObject
{
    private readonly ILogger logger;

    private int newDocumentCounter = 1;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentHostViewModel" /> class.
    /// </summary>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public DocumentHostViewModel(ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentHostViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentHostViewModel>();

        this.LogConstructed();

        // Commands are generated via CommunityToolkit.Mvvm [RelayCommand] applied to methods.
        // The generated command properties are exposed under the existing public names
        // via the wrapper properties below.

        // Example: add an initial document which cannot be closed
        var initial = new TabbedDocumentItem("Welcome", () => new TextBlock { Text = "Welcome to the editor" }) { IsClosable = false };
        this.TabbedDocuments.Add(initial);
        this.SelectedDocumentIndex = 0;
    }

    /// <summary>
    /// Raised when the VM has detached a document and the view should open it in a new window.
    /// The VM is responsible for updating its collection; the view is responsible for creating UI (a window).
    /// </summary>
    public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

    public ObservableCollection<TabbedDocumentItem> TabbedDocuments { get; } = [];

    public TabbedDocumentItem? SelectedDocument
        => this.SelectedDocumentIndex >= 0 && this.SelectedDocumentIndex < this.TabbedDocuments.Count
            ? this.TabbedDocuments[this.SelectedDocumentIndex]
            : null;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(CloseSelectedDocumentCommand))]
    [NotifyPropertyChangedFor(nameof(SelectedDocument))]
    public partial int SelectedDocumentIndex { get; set; }

    private static bool CanDetach(TabbedDocumentItem? doc) => doc is { IsClosable: true, IsPinned: false };

    private static bool CanClose(TabbedDocumentItem? doc) => doc is { IsClosable: true };

    [RelayCommand]
    private void AddNewDocument()
    {
        var title = $"Document {this.newDocumentCounter++}";
        var doc = new TabbedDocumentItem(title, () => new TextBlock { Text = title });
        this.TabbedDocuments.Add(doc);
        this.LogDocumentAdded(doc);
        this.SelectedDocumentIndex = this.TabbedDocuments.Count - 1;
    }

    [RelayCommand(CanExecute = nameof(CanClose))]
    private void CloseDocument(TabbedDocumentItem doc)
    {
        ArgumentNullException.ThrowIfNull(doc);

        if (!CanClose(doc))
        {
            this.LogCannotClose(doc);
            return;
        }

        var index = this.TabbedDocuments.IndexOf(doc);
        if (index < 0)
        {
            return;
        }

        if (ReferenceEquals(this.SelectedDocument, doc))
        {
            this.SelectPreviousDocumentItem(index);
        }

        this.TabbedDocuments.RemoveAt(index);
        this.LogTabbedDocumentClosed(doc);
    }

    [RelayCommand(CanExecute = nameof(CanCloseSelectedDocument))]
    private void CloseSelectedDocument()
    {
        if (this.SelectedDocument is not null)
        {
            this.CloseDocument(this.SelectedDocument);
        }
    }

    private bool CanCloseSelectedDocument() => CanClose(this.SelectedDocument);

    [RelayCommand(CanExecute = nameof(CanDetach))]
    private void DetachDocument(TabbedDocumentItem doc)
    {
        ArgumentNullException.ThrowIfNull(doc);

        if (!CanClose(doc))
        {
            this.LogCannotDetach(doc);
            return;
        }

        var index = this.TabbedDocuments.IndexOf(doc);
        if (index < 0)
        {
            return;
        }

        if (ReferenceEquals(this.SelectedDocument, doc))
        {
            this.SelectPreviousDocumentItem(this.TabbedDocuments.IndexOf(doc));
        }

        this.TabbedDocuments.RemoveAt(index);
        this.LogTabbedDocumentDetached(doc);

        // notify listeners (view) that a document was detached and UI should create a window for it
        this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs { Document = doc });
    }

    [RelayCommand(CanExecute = nameof(CanDetachSelectedDocument))]
    private void DetachSelectedDocument()
    {
        if (this.SelectedDocument is not null)
        {
            this.DetachDocument(this.SelectedDocument);
        }
    }

    private bool CanDetachSelectedDocument() => CanDetach(this.SelectedDocument);

    [RelayCommand]
    private void AttachDocument(TabbedDocumentItem doc)
    {
        ArgumentNullException.ThrowIfNull(doc);

        // Avoid duplicates if the document is already tracked
        if (this.TabbedDocuments.Contains(doc))
        {
            this.LogDocumentAlreadyAttached(doc);
            return;
        }

        this.TabbedDocuments.Add(doc);
        this.LogDocumentAttached(doc);
        this.SelectedDocumentIndex = this.TabbedDocuments.Count - 1;
    }

    private void SelectPreviousDocumentItem(int index)
    {
        Debug.Assert(index >= 0, "Index must be non-negative.");
        Debug.Assert(this.SelectedDocument is not null, "SelectedDocument must not be null when selecting previous document.");
        Debug.Assert(this.TabbedDocuments.IndexOf(this.SelectedDocument) == index, "given index must correspond to the selected item index.");

        if (this.TabbedDocuments.Count == 0)
        {
            this.SelectedDocumentIndex = -1;
            return;
        }

        this.SelectedDocumentIndex = index > 0 ? index - 1 : 0;
    }

    // This partial method is automatically called whenever SelectedDocument changes
    partial void OnSelectedDocumentIndexChanged(int oldValue, int newValue)
    {
        this.LogSelectionChanged(oldValue, newValue);
    }
}
