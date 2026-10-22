// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     The page of a document window: the document whose tab is selected there, which the user can edit to see its tab
///     follow its state in whichever window it is.
/// </summary>
/// <param name="documents">The sample's documents.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel classes must be public")]
public sealed partial class DocumentPageViewModel(DemoDocumentTabSource documents) : ObservableObject, IRoutingAware, IDisposable
{
    private WindowId window;
    private DispatcherQueue? dispatcher;

    /// <summary>Gets the document whose tab is selected in the window, if any.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ToggleEditedCommand))]
    public partial SampleDocumentMetadata? Document { get; private set; }

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        ArgumentNullException.ThrowIfNull(navigationContext);
        if (navigationContext.NavigationTarget is Window target)
        {
            this.window = target.AppWindow.Id;
            this.dispatcher = target.DispatcherQueue;
            documents.DocumentActivated += this.OnDocumentActivated;
            documents.DocumentClosed += this.OnDocumentClosed;
            documents.DocumentDetached += this.OnDocumentDetached;
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        documents.DocumentActivated -= this.OnDocumentActivated;
        documents.DocumentClosed -= this.OnDocumentClosed;
        documents.DocumentDetached -= this.OnDocumentDetached;
    }

    // An edit marks the document unsaved, and a second one saves it: its tab follows, in whichever window it is.
    [RelayCommand(CanExecute = nameof(HasDocument))]
    private void ToggleEdited()
    {
        if (this.Document is { } document)
        {
            document.IsDirty = !document.IsDirty;
        }
    }

    private bool HasDocument() => this.Document is not null;

    private void OnDocumentActivated(object? sender, DocumentActivatedEventArgs args)
    {
        if (args.WindowId == this.window)
        {
            this.Show(documents.GetOpenDocuments(this.window).FirstOrDefault(document => document.DocumentId == args.DocumentId));
        }
    }

    private void OnDocumentClosed(object? sender, DocumentClosedEventArgs args) => this.OnDocumentLeft(args.WindowId, args.Metadata);

    private void OnDocumentDetached(object? sender, DocumentDetachedEventArgs args) => this.OnDocumentLeft(args.WindowId, args.Metadata);

    private void OnDocumentLeft(WindowId from, IDocumentMetadata document)
    {
        if (from == this.window && document.DocumentId == this.Document?.DocumentId)
        {
            this.Show(document: null);
        }
    }

    private void Show(IDocumentMetadata? document)
        => _ = this.dispatcher?.TryEnqueue(() => this.Document = document as SampleDocumentMetadata);
}
