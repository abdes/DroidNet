// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Microsoft.UI.Dispatching;

namespace Oxygen.Editor.Cooking;

/// <summary>
///     Keeps each run's selected messages across run switches and hiding. The console owns scrolling and following
///     the end of its own transcript; the run keeps whether it follows.
/// </summary>
public sealed partial class CookingPanelView
{
    private CookingPanelViewModel? readingModel;
    private CookingRunViewModel? readingRun;
    private bool restoreSelectionQueued;

    private void InitializeReadingTracking()
    {
        this.Loaded += (_, _) => this.SubscribeReadingModel();
        this.Unloaded += (_, _) => this.UnsubscribeReadingModel();
        this.ViewModelChanged += (_, _) => this.SubscribeReadingModel();
    }

    private void SubscribeReadingModel()
    {
        this.UnsubscribeReadingModel();
        if (!this.IsLoaded || this.ViewModel is not { } model)
        {
            return;
        }

        this.readingModel = model;
        model.PropertyChanging += this.OnReadingModelChanging;
        model.PropertyChanged += this.OnReadingModelChanged;
        this.SelectReadingRun();
    }

    private void UnsubscribeReadingModel()
    {
        this.CaptureOutputSelection();
        if (this.readingModel is { } model)
        {
            model.PropertyChanging -= this.OnReadingModelChanging;
            model.PropertyChanged -= this.OnReadingModelChanged;
        }

        this.readingModel = null;
        this.readingRun = null;
    }

    private void OnReadingModelChanging(object? sender, PropertyChangingEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(CookingPanelViewModel.SelectedRun), StringComparison.Ordinal))
        {
            this.CaptureOutputSelection();
        }
    }

    private void OnReadingModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(CookingPanelViewModel.SelectedRun), StringComparison.Ordinal))
        {
            this.SelectReadingRun();
        }
    }

    private void CaptureOutputSelection()
    {
        if (this.readingRun is { } run)
        {
            run.SelectedOutputEntries = this.CookOutput.CaptureSelection();
        }
    }

    private void SelectReadingRun()
    {
        this.readingRun = this.readingModel?.SelectedRun;
        if (this.readingRun is not null && !this.restoreSelectionQueued)
        {
            this.restoreSelectionQueued = this.DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, this.RestoreOutputSelection);
        }
    }

    private void RestoreOutputSelection()
    {
        this.restoreSelectionQueued = false;

        // A selection made since the restore was queued is newer than the one the run kept.
        if (this.IsLoaded && this.readingRun is { } run && this.CookOutput.CaptureSelection().Count == 0)
        {
            this.CookOutput.RestoreSelection(run.SelectedOutputEntries);
        }
    }
}
