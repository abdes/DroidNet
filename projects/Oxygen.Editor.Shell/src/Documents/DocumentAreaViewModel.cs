// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The view model of a window's document area: it shows the view of the active document's editor, and the banner of
///     the active document when its file is missing or changed on disk.
/// </summary>
/// <remarks>
///     The document service owns the editors; the area only shows them. The view of the editor that leaves is deactivated
///     before the next one shows, so an editor that holds native surfaces releases them first.
/// </remarks>
public sealed partial class DocumentAreaViewModel : ObservableObject, IDisposable
{
    private readonly ILogger logger;
    private readonly IDocumentService documents;
    private readonly IDocumentArea area;
    private readonly IDocumentConfirmations confirmations;
    private readonly IViewLocator viewLocator;

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A view switch in flight releases the gate after Dispose; the gate holds no wait handle.")]
    private readonly SemaphoreSlim activeEditorViewGate = new(initialCount: 1, maxCount: 1);

    private long activeEditorViewRequestId;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentAreaViewModel" /> class.
    /// </summary>
    /// <param name="documents">The document service of the area.</param>
    /// <param name="area">The document area of the window.</param>
    /// <param name="confirmations">The window's confirmations, which the banner's actions ask.</param>
    /// <param name="viewLocator">Finds the view of an editor.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public DocumentAreaViewModel(
        IDocumentService documents,
        IDocumentArea area,
        IDocumentConfirmations confirmations,
        IViewLocator viewLocator,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(area);
        ArgumentNullException.ThrowIfNull(confirmations);
        ArgumentNullException.ThrowIfNull(viewLocator);
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<DocumentAreaViewModel>();
        this.documents = documents;
        this.area = area;
        this.confirmations = confirmations;
        this.viewLocator = viewLocator;

        this.documents.ActiveEditorChanged += this.OnServiceActiveEditorChanged;
        this.area.ActiveDocumentChanged += this.OnActiveDocumentChanged;
        this.ActiveEditor = this.documents.ActiveEditor;
        this.ObserveActiveDocument(this.area.ActiveDocument);
    }

    /// <summary>
    ///     Gets the editor of the active document, or <see langword="null"/> when no document is active.
    /// </summary>
    [ObservableProperty]
    public partial object? ActiveEditor { get; private set; }

    /// <summary>
    ///     Gets the view of <see cref="ActiveEditor"/>, once the previous editor's view has deactivated.
    /// </summary>
    [ObservableProperty]
    public partial object? ActiveEditorView { get; private set; }

    /// <summary>
    ///     Stops following the area. The editors belong to the document service.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.ActiveEditorChanged -= this.OnServiceActiveEditorChanged;
        this.area.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
        this.ObserveActiveDocument(null);
        _ = Interlocked.Increment(ref this.activeEditorViewRequestId);
    }

    private void OnServiceActiveEditorChanged(object? sender, EventArgs e) => this.ActiveEditor = this.documents.ActiveEditor;

    partial void OnActiveEditorChanged(object? value)
    {
        if (this.disposed)
        {
            return;
        }

        var requestId = Interlocked.Increment(ref this.activeEditorViewRequestId);
        _ = this.SwitchActiveEditorViewAsync(value, requestId);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "View teardown failures are logged so an approved document transition can finish without crashing the UI.")]
    private async Task SwitchActiveEditorViewAsync(object? value, long requestId)
    {
        await this.activeEditorViewGate.WaitAsync().ConfigureAwait(true);
        try
        {
            if (!this.IsCurrent(value, requestId))
            {
                return;
            }

            if (this.ActiveEditorView is IDocumentEditorView previousView)
            {
                try
                {
                    await previousView.DeactivateAsync().ConfigureAwait(true);
                }
                catch (Exception ex)
                {
                    this.LogViewDeactivationFailed(ex);
                }
            }

            if (!this.IsCurrent(value, requestId))
            {
                return;
            }

            if (value is null)
            {
                this.ActiveEditorView = null;
                return;
            }

            var view = this.viewLocator.ResolveView(value);
            if (view is IViewFor nonGeneric)
            {
                nonGeneric.ViewModel = value;
            }

            if (!this.IsCurrent(value, requestId))
            {
                if (view is IDocumentEditorView staleView)
                {
                    await staleView.DeactivateAsync().ConfigureAwait(true);
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

    private bool IsCurrent(object? value, long requestId)
        => !this.disposed && requestId == Volatile.Read(ref this.activeEditorViewRequestId) && ReferenceEquals(value, this.ActiveEditor);
}
