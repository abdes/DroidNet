// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
///     ViewModel for the New Project dialog in the Oxygen Editor's Project Browser.
/// </summary>
public partial class NewProjectDialogViewModel : ObservableObject, IDisposable
{
    // cancellation for the auto-hide timer
    private CancellationTokenSource? feedbackMessageCts;

    /// <summary>
    ///     Initializes a new instance of the <see cref="NewProjectDialogViewModel" /> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="template">The template information for the new project.</param>
    public NewProjectDialogViewModel(IProjectBrowserService projectBrowser, ITemplateInfo template)
    {
        this.Template = template;

        this.PinnedLocations = projectBrowser.GetQuickSaveLocations();
        this.SelectedLocation = this.PinnedLocations[0];

        this.ProjectName = string.Empty;
        this.FeedbackMessage = string.Empty;
        this.IsFeedbackMessageVisible = false;
    }

    [ObservableProperty]
    public partial string FeedbackMessage { get; set; } = string.Empty;

    [ObservableProperty]
    public partial bool IsFeedbackMessageVisible { get; set; }

    [ObservableProperty]
    public partial IList<QuickSaveLocation> PinnedLocations { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsProjectNameValid))]
    public partial string ProjectName { get; set; }

    [ObservableProperty]
    public partial QuickSaveLocation SelectedLocation { get; set; }

    [ObservableProperty]
    public partial ITemplateInfo Template { get; set; }

    [ObservableProperty]
    public partial bool IsActivating { get; set; }

    /// <summary>
    ///     Gets a value indicating whether the project name is valid.
    /// </summary>
    public bool IsProjectNameValid
        => !string.IsNullOrEmpty(this.ProjectName); // TODO: validate project name, use CanCreateProject

    /// <summary>
    ///     Show an error message related to the picker. The message will auto-hide after the specified timeout (ms).
    /// </summary>
    /// <param name="message">The message to display.</param>
    /// <param name="timeoutMs">Timeout in milliseconds before auto-hiding. Defaults to 10000 (10s).</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    public async Task ShowFeedbackMessageAsync(string message, int timeoutMs = 5000)
    {
        // safely cancel existing CTS and await its async cancel completion
        var oldCts = this.feedbackMessageCts;
        if (oldCts != null)
        {
            await oldCts.CancelAsync().ConfigureAwait(true);
            oldCts.Dispose();
        }

        this.feedbackMessageCts = new CancellationTokenSource();

        // Set properties (on calling thread, usually UI thread)
        this.FeedbackMessage = message;
        this.IsFeedbackMessageVisible = true;

        try
        {
            // Await the timeout using the CTS. Do NOT use ConfigureAwait(false) here so the
            // continuation runs back on the UI thread and property changes are observed.
            await Task.Delay(timeoutMs, this.feedbackMessageCts.Token).ConfigureAwait(true);

            // Hide
            this.IsFeedbackMessageVisible = false;
            this.FeedbackMessage = string.Empty;
        }
        catch (OperationCanceledException)
        {
            // cancelled by new message or manual close
        }
        finally
        {
            this.feedbackMessageCts?.Dispose();
            this.feedbackMessageCts = null;
        }
    }

    /// <summary>
    ///     Close the picker error immediately.
    /// </summary>
    public void ClosePickerError()
    {
        this.feedbackMessageCts?.Cancel();
        this.feedbackMessageCts?.Dispose();
        this.feedbackMessageCts = null;

        this.IsFeedbackMessageVisible = false;
        this.FeedbackMessage = string.Empty;
    }

    /// <summary>
    ///     Releases resources used by the <see cref="NewProjectDialogViewModel"/>.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Resets the activation state.
    /// </summary>
    internal void ResetActivationState() => this.IsActivating = false;

    /// <summary>
    ///     Releases unmanaged and optionally managed resources.
    /// </summary>
    /// <param name="disposing">True to release both managed and unmanaged resources; false to release only unmanaged resources.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.feedbackMessageCts?.Cancel();
            this.feedbackMessageCts?.Dispose();
            this.feedbackMessageCts = null;
        }
    }

    /// <summary>
    ///     Sets the selected location.
    /// </summary>
    /// <param name="location">The location to set as selected.</param>
    [RelayCommand]
    private void SetLocation(QuickSaveLocation location)
        => this.SelectedLocation = location;

    // Called by source generator when IsPickerErrorVisible changes
    partial void OnIsFeedbackMessageVisibleChanged(bool value)
    {
        if (value)
        {
            return;
        }

        // clear message and cancel any pending timer
        this.feedbackMessageCts?.Cancel();
        this.feedbackMessageCts?.Dispose();
        this.feedbackMessageCts = null;
        this.FeedbackMessage = string.Empty;
    }
}
