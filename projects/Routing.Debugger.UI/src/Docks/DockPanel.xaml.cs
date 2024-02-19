// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Docks;

using System.Reactive.Linq;
using DroidNet.Routing.Generators;
using Microsoft.UI.Xaml;

/// <summary>A decorated panel that represents a dock.</summary>
[ViewModel(typeof(DockPanelViewModel))]
public sealed partial class DockPanel
{
    private const double ResizeThrottleInMs = 500.0;
    private IDisposable? sizeChangedSubscription;

    public DockPanel() => this.InitializeComponent();

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;
        this.sizeChangedSubscription = Observable.FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
                h => this.SizeChanged += h,
                h => this.SizeChanged -= h)
            .Throttle(TimeSpan.FromMilliseconds(ResizeThrottleInMs))
            .Subscribe(evt => this.DispatcherQueue.TryEnqueue(() => this.OnSizeChanged(evt.EventArgs)));
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        this.sizeChangedSubscription?.Dispose();
    }

    private void OnSizeChanged(SizeChangedEventArgs args) => this.ViewModel.OnSizeChanged(args.NewSize);
}
