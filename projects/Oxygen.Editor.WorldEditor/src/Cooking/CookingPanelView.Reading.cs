// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Preserves each run's reading position and follows output through the shared details scroller.</summary>
public sealed partial class CookingPanelView
{
    private CookingPanelViewModel? readingModel;
    private CookingRunViewModel? readingRun;
    private bool restoreReadingQueued;
    private bool pendingOutputFollow;
    private bool restoreOutputSelection;
    private double observedOffset;
    private double observedExtent;

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

        if (this.readingRun is { } run)
        {
            run.Output.CollectionChanged -= this.OnRunOutputChanged;
        }

        this.readingModel = null;
        this.readingRun = null;
        this.pendingOutputFollow = false;
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
        if (this.readingRun is { } previous)
        {
            previous.Output.CollectionChanged -= this.OnRunOutputChanged;
        }

        this.readingRun = this.readingModel?.SelectedRun;
        this.pendingOutputFollow = false;
        this.restoreOutputSelection = true;
        if (this.readingRun is { } next)
        {
            next.Output.CollectionChanged += this.OnRunOutputChanged;
            this.QueueReadingRestore();
        }
    }

    private void OnRunOutputChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.Action == NotifyCollectionChangedAction.Add && this.readingRun is { FollowTail: true, IsOutputExpanded: true })
        {
            this.pendingOutputFollow = true;
        }
    }

    private void OnDetailsContentSizeChanged(object sender, SizeChangedEventArgs args)
    {
        if (this.pendingOutputFollow && this.readingRun?.FollowTail == true)
        {
            this.QueueReadingRestore();
        }
    }

    private void OnDetailsViewChanged(object? sender, ScrollViewerViewChangedEventArgs args)
    {
        if (!this.IsLoaded || this.readingRun is not { } run || this.restoreReadingQueued)
        {
            return;
        }

        var offset = this.DetailsScroller.VerticalOffset;
        var extent = this.DetailsScroller.ScrollableHeight;
        if (Math.Abs(offset - this.observedOffset) > 0.5 && Math.Abs(extent - this.observedExtent) < 0.5)
        {
            run.ReadingOffset = offset;
            run.FollowTail = extent - offset <= 1;
            run.HasReadingPosition = true;
            this.pendingOutputFollow = false;
        }

        this.observedOffset = offset;
        this.observedExtent = extent;
    }

    private void QueueReadingRestore()
    {
        if (this.restoreReadingQueued)
        {
            return;
        }

        this.restoreReadingQueued = this.DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, this.RestoreReadingPosition);
    }

    private void RestoreReadingPosition()
    {
        this.restoreReadingQueued = false;
        if (!this.IsLoaded || this.readingRun is not { } run)
        {
            return;
        }

        this.DetailsScroller.UpdateLayout();
        var extent = this.DetailsScroller.ScrollableHeight;
        var offset = run.HasReadingPosition ? run.FollowTail ? extent : Math.Clamp(run.ReadingOffset, 0, extent) : 0;
        this.observedOffset = offset;
        this.observedExtent = extent;
        _ = this.DetailsScroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
        run.ReadingOffset = offset;
        run.FollowTail = extent - offset <= 1;
        run.HasReadingPosition = true;
        this.pendingOutputFollow = false;
        if (this.restoreOutputSelection)
        {
            this.CookOutput.RestoreSelection(run.SelectedOutputEntries);
            this.restoreOutputSelection = false;
        }
    }
}
