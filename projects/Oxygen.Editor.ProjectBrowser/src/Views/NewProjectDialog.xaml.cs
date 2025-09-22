// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
///     A dialog for creating a new project.
/// </summary>
public sealed partial class NewProjectDialog
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="NewProjectDialog" /> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="template">The template information for the new project.</param>
    public NewProjectDialog(IProjectBrowserService projectBrowser, ITemplateInfo template)
    {
        this.InitializeComponent();

        this.ViewModel = new NewProjectDialogViewModel(projectBrowser, template);
        this.DataContext = this.ViewModel;

        _ = this.ProjectNameTextBox.Focus(FocusState.Programmatic);

        // attach InfoBar close handler
        this.FeedbackMessageInfoBar.CloseButtonClick += (_, __) => this.ViewModel.ClosePickerError();
    }

    /// <summary>
    ///     Gets or sets the view model for the dialog.
    /// </summary>
    public NewProjectDialogViewModel ViewModel { get; set; }

    [LibraryImport("user32.dll")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial IntPtr GetForegroundWindow();

    [LibraryImport("user32.dll")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial IntPtr GetActiveWindow();

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

            var picker = new FolderPicker();

            // Prefer App window handle if available via WindowNative. Fallback to GetForegroundWindow/GetActiveWindow.
            var hwnd = GetForegroundWindow() is var h && h != IntPtr.Zero ? h : GetActiveWindow();
            InitializeWithWindow.Initialize(picker, hwnd);

            // Optionally set suggested start location and file type filter
            picker.SuggestedStartLocation = PickerLocationId.Desktop;
            picker.FileTypeFilter.Add("*");

            var folder = await picker.PickSingleFolderAsync();
            if (folder is null)
            {
                return;
            }

            var location = new QuickSaveLocation("Custom", folder.Path);
            this.ViewModel.SetLocationCommand.Execute(location);
            this.LocationExpander.IsExpanded = false;
        }
        catch (Exception ex)
        {
            // show the error message in the ViewModel-bound InfoBar and log
            _ = this.ViewModel.ShowFeedbackMessageAsync("Could not open folder picker. Please try again.");

            // Ideally also log the exception to application telemetry
            Debug.WriteLine(ex);
        }
    }
}
