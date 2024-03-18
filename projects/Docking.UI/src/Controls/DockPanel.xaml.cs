// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using System.Reactive.Linq;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Windows.Foundation;

/// <summary>A decorated panel that represents a dock.</summary>
[ViewModel(typeof(DockPanelViewModel))]
public sealed partial class DockPanel
{
    private const double ResizeThrottleInMs = 100.0;
    private IDisposable? sizeChangedSubscription;

    public DockPanel()
    {
        this.InitializeComponent();

        this.Loaded += (_, _) =>
        {
            // Register for size changes, but we don't want to trigger the effect on every change when a dock is being
            // continuously resized. So, we throttle the events and only react after a stable size is reached.
            this.sizeChangedSubscription = Observable.FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
                    h => this.SizeChanged += h,
                    h => this.SizeChanged -= h)
                .Throttle(TimeSpan.FromMilliseconds(ResizeThrottleInMs))
                .Subscribe(
                    evt => this.DispatcherQueue.TryEnqueue(() => this.ViewModel?.OnSizeChanged(evt.EventArgs.NewSize)));

            // We should always set the initial size in the view model after the view is loaded, and everytime the view
            // model changes.
            this.ViewModel?.OnSizeChanged(this.GetActualSize());
            this.ViewModelChanged += (_, _) => this.ViewModel?.OnSizeChanged(this.GetActualSize());
        };

        this.Unloaded += (_, _) => this.sizeChangedSubscription?.Dispose();
    }

    public override string ToString() => $"{nameof(DockPanel)} [{this.ViewModel?.Title ?? string.Empty}]";

    private Size GetActualSize() => new()
    {
        Width = double.Round(this.ActualWidth),
        Height = double.Round(this.ActualHeight),
    };
}
