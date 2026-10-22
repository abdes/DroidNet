// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     Opens the sample's document windows: routed windows whose shell shows the tabs of the sample's documents. A tab
///     dropped outside every strip goes to one opened where it was dropped.
/// </summary>
/// <param name="router">The application's router, which opens a window on the document page.</param>
/// <param name="windows">The window manager, which binds the window's tabs and moves it.</param>
/// <param name="documents">The sample's documents.</param>
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "The sample's view models take it")]
public sealed class DemoDocumentWindows(IRouter router, IWindowManagerService windows, DemoDocumentTabSource documents) : IDocumentWindowFactory
{
    /// <summary>The page of a document window.</summary>
    public const string Page = "/doc";

    /// <summary>Gets the target the router opens a new document window in, for each navigation.</summary>
    public static Target Target { get; } = new() { Name = "document" };

    /// <inheritdoc/>
    public async Task<WindowId?> CreateWindowAsync(IDocumentMetadata document, SpatialPoint<PhysicalScreenSpace> position)
    {
        if (await this.OpenAsync().ConfigureAwait(true) is not { } window)
        {
            return null;
        }

        await windows.MoveWindowAsync(window, new PointInt32((int)Math.Round(position.Point.X), (int)Math.Round(position.Point.Y))).ConfigureAwait(true);
        return window;
    }

    /// <summary>Opens a document window, with no tab yet.</summary>
    /// <returns>The window, or <see langword="null"/> when it did not open.</returns>
    public async Task<WindowId?> OpenAsync()
    {
        var navigation = new FullNavigation { Target = Target };
        Window? opened = null;
        using (router.Events.OfType<ActivationComplete>()
            .Where(activation => ReferenceEquals(activation.Options, navigation))
            .Subscribe(activation => opened = activation.Context.NavigationTarget as Window))
        {
            await router.NavigateAsync(Page, navigation).ConfigureAwait(true);
        }

        if (opened is null || windows.GetWindow(opened.AppWindow.Id) is not { } managed)
        {
            return null;
        }

        managed.DocumentTabSource = documents;
        return managed.Id;
    }
}
