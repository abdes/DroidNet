// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Windows.UI.ViewManagement;

namespace DroidNet.Controls;

/// <summary>Shares the system text scale factor and re-lays out the elements whose layout depends on it.</summary>
/// <remarks>
///     One <see cref="UISettings"/> instance serves every control, instead of one per control instance. The system
///     raises its change notification on a background thread; each registered element is re-measured on its own
///     dispatcher queue, and only while it is loaded, so the shared notification never keeps an unloaded element alive.
/// </remarks>
public static class TextScale
{
    private static readonly UISettings Settings = CreateSettings();

    private static event EventHandler? Changed;

    /// <summary>Gets the current system text scale factor, 1 at the default text size.</summary>
    public static double Factor => Settings.TextScaleFactor;

    /// <summary>Re-measures an element whenever the system text scale factor changes while the element is loaded.</summary>
    /// <param name="element">An element whose measurement uses <see cref="Factor"/>.</param>
    public static void InvalidateMeasureOnChange(FrameworkElement element)
    {
        ArgumentNullException.ThrowIfNull(element);

        var subscription = new Subscription(element);
        element.Loaded += (_, _) => subscription.Attach();
        element.Unloaded += (_, _) => subscription.Detach();
    }

    private static UISettings CreateSettings()
    {
        var created = new UISettings();
        created.TextScaleFactorChanged += (_, _) => Changed?.Invoke(sender: null, EventArgs.Empty);
        return created;
    }

    // Holds one element's handler; the dispatcher queue is read on the UI thread, when the element loads.
    private sealed class Subscription(FrameworkElement element)
    {
        private DispatcherQueue? queue;

        public void Attach()
        {
            this.queue = element.DispatcherQueue;
            Changed -= this.OnChanged;
            Changed += this.OnChanged;
        }

        public void Detach() => Changed -= this.OnChanged;

        private void OnChanged(object? sender, EventArgs args) => _ = this.queue?.TryEnqueue(element.InvalidateMeasure);
    }
}
