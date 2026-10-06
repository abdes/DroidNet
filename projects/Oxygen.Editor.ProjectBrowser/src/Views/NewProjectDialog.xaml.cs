// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Dialogs;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
///     A dialog for creating a new project.
/// </summary>
public sealed partial class NewProjectDialog
{
    private readonly IDialogService dialogService;
    private readonly ILogger logger;
    private Grid? contentGrid;

    /// <summary>
    ///     Initializes a new instance of the <see cref="NewProjectDialog" /> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="template">The template information for the new project.</param>
    /// <param name="dialogService">The owner-aware picker service.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public NewProjectDialog(
        IProjectBrowserService projectBrowser,
        ITemplateInfo template,
        IDialogService dialogService,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(dialogService);
        this.dialogService = dialogService;
        this.logger = loggerFactory?.CreateLogger<NewProjectDialog>() ?? NullLogger<NewProjectDialog>.Instance;
        this.InitializeComponent();

        this.ViewModel = new NewProjectDialogViewModel(projectBrowser, template);
        this.DataContext = this.ViewModel;

        _ = this.ProjectNameTextBox.Focus(FocusState.Programmatic);

        // attach InfoBar close handler
        this.FeedbackMessageInfoBar.CloseButtonClick += (_, __) => this.ViewModel.ClosePickerError();

        this.Loaded += this.OnDialogLoaded;
    }

    /// <summary>
    ///     Gets or sets the view model for the dialog.
    /// </summary>
    public NewProjectDialogViewModel ViewModel { get; set; }

    internal async Task BrowseLocationAsync()
    {
        var ownerWindowId = this.XamlRoot?.ContentIslandEnvironment.AppWindowId
            ?? throw new InvalidOperationException("The project dialog must be attached to an owner window.");
        var picker = new FolderPickerSpec("Choose project location", "Oxygen.ProjectLocation")
        {
            SuggestedStartFolder = this.ViewModel.SelectedLocation.Path,
        };
        var path = await this.dialogService.PickFolderAsync(picker, ownerWindowId).ConfigureAwait(true);
        if (path is not null)
        {
            this.ViewModel.SetLocationCommand.Execute(new QuickSaveLocation("Custom", path));
            this.LocationExpander.IsExpanded = false;
        }
    }

    private void OnDialogLoaded(object sender, RoutedEventArgs e)
    {
        this.contentGrid = this.FindName("DialogContentGrid") as Grid;
        this.ViewModel.PropertyChanged += this.OnViewModelPropertyChanged;
    }

    private void OnViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(NewProjectDialogViewModel.IsActivating), StringComparison.Ordinal))
        {
            this.UpdateDialogState();
        }
    }

    private void UpdateDialogState()
    {
        if (this.contentGrid is { } grid)
        {
            grid.IsHitTestVisible = !this.ViewModel.IsActivating;
        }
    }

    /// <summary>
    ///     Handles the click event of a location item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The event data.</param>
    private void OnLocationItemClick(object sender, ItemClickEventArgs e)
    {
        _ = sender;
        this.ViewModel.SetLocationCommand.Execute(e.ClickedItem);
        this.LocationExpander.IsExpanded = false;
    }

    private async void OnBrowseClick(object sender, RoutedEventArgs e)
    {
        try
        {
            _ = sender;
            _ = e;
            await this.BrowseLocationAsync().ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException
            or InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            LogFailedToOpenPicker(this.logger, ex);
            await this.ViewModel.ShowFeedbackMessageAsync("Could not open folder picker. Please try again.").ConfigureAwait(true);
        }
    }
}
