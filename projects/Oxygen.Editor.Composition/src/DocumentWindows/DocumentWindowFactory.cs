// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace Oxygen.Editor.DocumentWindows;

/// <summary>
///     Opens the document window of a tab torn out of one of the session's windows, at the point where the tab was
///     dropped. Aura's document transfers attach the document to it.
/// </summary>
/// <param name="router">The application's router, which opens the window on its page.</param>
/// <param name="windows">The window manager, which moves the window.</param>
/// <param name="loggerFactory">Optional factory of the service's logger.</param>
internal sealed partial class DocumentWindowFactory(IRouter router, IWindowManagerService windows, ILoggerFactory? loggerFactory = null) : IDocumentWindowFactory
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<DocumentWindowFactory>() ?? NullLogger<DocumentWindowFactory>.Instance;

    /// <inheritdoc />
    public async Task<WindowId?> CreateWindowAsync(IDocumentMetadata document, SpatialPoint<PhysicalScreenSpace> position)
    {
        ArgumentNullException.ThrowIfNull(document);

        // The router creates a new window for each navigation to the document target.
        var navigation = new FullNavigation { Target = new Target { Name = EditorWindows.DocumentTarget } };
        Window? opened = null;
        using (router.Events.OfType<ActivationComplete>()
            .Where(activation => ReferenceEquals(activation.Options, navigation))
            .Subscribe(activation => opened = activation.Context.NavigationTarget as Window))
        {
            if (!await EditorWindows.NavigateAsync(router, EditorWindows.DocumentPage, navigation).ConfigureAwait(true) || opened is null)
            {
                this.LogNotOpened(document.Title);
                return null;
            }
        }

        var id = opened.AppWindow.Id;
        await windows.MoveWindowAsync(id, new PointInt32((int)Math.Round(position.Point.X), (int)Math.Round(position.Point.Y))).ConfigureAwait(true);
        this.LogOpened(document.Title, id);
        return id;
    }
}
