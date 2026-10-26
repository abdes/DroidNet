// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Controls;
using Oxygen.Managed.Core.Diagnostics;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The Import model dialog's content: the file list and the import settings panel, separated by a splitter, with
/// Before import in a flyout of the facts bar's action.</summary>
public sealed partial class ModelImportDialogView : UserControl
{
    private bool selecting;
    private IReadOnlyList<ModelImportFile>? pendingSelection;

    /// <summary>Initializes a new instance of the <see cref="ModelImportDialogView"/> class.</summary>
    /// <param name="viewModel">The dialog's view model.</param>
    public ModelImportDialogView(ModelImportDialogViewModel viewModel)
    {
        this.ViewModel = viewModel ?? throw new ArgumentNullException(nameof(viewModel));
        this.InitializeComponent();

        // The view lives as long as its dialog and view model, so it listens for their whole life: a content dialog
        // unloads and loads its content again, and not always in that order. Each load shows the dialog's selection.
        this.ViewModel.SelectionRequested += this.OnSelectionRequested;
        this.Loaded += (_, _) => this.Select(this.ViewModel.Selection);
    }

    /// <summary>Gets the dialog's view model.</summary>
    public ModelImportDialogViewModel ViewModel { get; }

    /// <summary>Gets the glyph of a file's status: a warning or an error.</summary>
    /// <param name="status">The file's status.</param>
    /// <returns>The glyph.</returns>
    internal static string StatusGlyph(ModelImportFileStatus status) => status == ModelImportFileStatus.Error ? "" : "";

    /// <summary>Gets the brush of a file's status glyph.</summary>
    /// <param name="status">The file's status.</param>
    /// <returns>The critical or caution brush.</returns>
    internal static Brush? StatusBrush(ModelImportFileStatus status)
        => Application.Current.Resources.TryGetValue(
            status == ModelImportFileStatus.Error ? "SystemFillColorCriticalBrush" : "SystemFillColorCautionBrush", out var brush)
            ? brush as Brush
            : null;

    /// <summary>Shows a file's status glyph only for a warning or an error.</summary>
    /// <param name="status">The file's status.</param>
    /// <returns>Visible unless the file is ready.</returns>
    internal static Visibility StatusVisibility(ModelImportFileStatus status) => status == ModelImportFileStatus.Ready ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets the card severity of a diagnostic.</summary>
    /// <param name="severity">The diagnostic's severity.</param>
    /// <returns>The card's severity.</returns>
    internal static DiagnosticCardSeverity SeverityOf(DiagnosticSeverity severity)
        => severity switch
        {
            >= DiagnosticSeverity.Error => DiagnosticCardSeverity.Error,
            DiagnosticSeverity.Warning => DiagnosticCardSeverity.Warning,
            _ => DiagnosticCardSeverity.Informational,
        };

    /// <summary>Gets the facts bar's severity: the most serious finding's.</summary>
    /// <param name="severity">The most serious finding's severity.</param>
    /// <returns>The bar's severity.</returns>
    internal static InfoBarSeverity BarSeverity(DiagnosticSeverity severity)
        => severity switch
        {
            >= DiagnosticSeverity.Error => InfoBarSeverity.Error,
            DiagnosticSeverity.Warning => InfoBarSeverity.Warning,
            _ => InfoBarSeverity.Informational,
        };

    /// <summary>Gets whether a line of text has any.</summary>
    /// <param name="text">The text.</param>
    /// <returns><see langword="true"/> when the text is not empty.</returns>
    internal static bool HasText(string? text) => !string.IsNullOrEmpty(text);

    /// <summary>Gets the placeholder of a field whose selected files differ.</summary>
    /// <param name="isMixed">Whether the files' values differ.</param>
    /// <returns>Mixed, or nothing.</returns>
    internal static string MixedPlaceholder(bool isMixed) => isMixed ? "Mixed" : string.Empty;

    private void OnSelectionRequested(object? sender, IReadOnlyList<ModelImportFile> files) => this.Select(files);

    // Shows the dialog's selection in the list. Files just added have no rows yet, and the first ones appear with the
    // list itself, so the selection is shown again once the list has laid out the last of them.
    private void Select(IReadOnlyList<ModelImportFile> files)
    {
        this.pendingSelection = files;
        this.ShowSelection(files);
        this.FileList.LayoutUpdated -= this.OnFileListLayoutUpdated;
        this.FileList.LayoutUpdated += this.OnFileListLayoutUpdated;
    }

    private void OnFileListLayoutUpdated(object? sender, object args)
    {
        if (this.pendingSelection is not { } files)
        {
            this.FileList.LayoutUpdated -= this.OnFileListLayoutUpdated;
            return;
        }

        if (files.Count != 0 && this.FileList.ContainerFromItem(files[^1]) is null)
        {
            return;
        }

        this.pendingSelection = null;
        this.FileList.LayoutUpdated -= this.OnFileListLayoutUpdated;
        this.ShowSelection(files);
    }

    private void ShowSelection(IReadOnlyList<ModelImportFile> files)
    {
        this.selecting = true;
        try
        {
            this.FileList.SelectedItems.Clear();
            foreach (var file in files)
            {
                this.FileList.SelectedItems.Add(file);
            }
        }
        finally
        {
            this.selecting = false;
        }

        if (files.Count != 0)
        {
            this.FileList.ScrollIntoView(files[^1]);
        }
    }

    private void OnFileSelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (!this.selecting)
        {
            this.ViewModel.UpdateSelection(this.FileList.SelectedItems.OfType<ModelImportFile>());
        }
    }

    // Files dragged from File Explorer are added; a drag within the list reorders it.
    private void OnFilesDragOver(object sender, DragEventArgs args)
    {
        if (args.DataView.Contains(StandardDataFormats.StorageItems))
        {
            args.AcceptedOperation = DataPackageOperation.Copy;
        }
    }

    private async void OnFilesDrop(object sender, DragEventArgs args)
    {
        if (!args.DataView.Contains(StandardDataFormats.StorageItems))
        {
            return;
        }

        var deferral = args.GetDeferral();
        try
        {
            var items = await args.DataView.GetStorageItemsAsync();
            _ = this.ViewModel.AddFiles(items.OfType<StorageFile>().Select(static file => file.Path));
        }
        finally
        {
            deferral.Complete();
        }
    }
}
