// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Default implementation of <see cref="IWindowFactory"/> that uses a service provider to
///     resolve window instances with dependency injection and registers them with the window
///     manager.
/// </summary>
public sealed partial class DefaultWindowFactory : IWindowFactory
{
    private readonly IContainer container;
    private readonly IWindowManagerService windowManagerService;
    private readonly ILogger<DefaultWindowFactory> logger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DefaultWindowFactory"/> class.
    /// </summary>
    /// <param name="container">The DI container for resolving window instances.</param>
    /// <param name="windowManagerService">The window manager service for registering created windows.</param>
    /// <param name="loggerFactory">Optional logger factory used to create a service logger.</param>
    public DefaultWindowFactory(
        IContainer container,
        IWindowManagerService windowManagerService,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(container);
        ArgumentNullException.ThrowIfNull(windowManagerService);

        this.container = container;
        this.windowManagerService = windowManagerService;
        this.logger = loggerFactory?.CreateLogger<DefaultWindowFactory>() ?? NullLogger<DefaultWindowFactory>.Instance;
    }

    /// <inheritdoc/>
    public async Task<TWindow> CreateWindow<TWindow>(IReadOnlyDictionary<string, object>? metadata = null)
        where TWindow : Window
    {
        this.LogCreateWindow(typeof(TWindow), widthMetadata: metadata != null);

        try
        {
            var window = this.container.Resolve<TWindow>();

            // Register the created window with the window manager.
            _ = await this.windowManagerService.RegisterWindowAsync(window, metadata).ConfigureAwait(false);

            this.LogWindowCreated(typeof(TWindow));
            return window;
        }
        catch (Exception ex)
        {
            this.LogCreateWindowFailed(ex, typeof(TWindow));
            throw new InvalidOperationException($"Failed to create window of type {typeof(TWindow).FullName}", ex);
        }
    }

    /// <inheritdoc/>
    public async Task<Window> CreateWindow(string key, IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        this.LogCreateKeyedWindow(key, widthMetadata: metadata != null);

        try
        {
            // Use keyed service resolution via the ServiceProvider
            var window = this.container.Resolve<Window>(key);

            // Register the created window with the window manager.
            _ = await this.windowManagerService.RegisterWindowAsync(window, metadata).ConfigureAwait(false);

            this.LogKeyedWindowCreated(key, window.GetType(), widthMetadata: metadata != null);
            return window;
        }
        catch (Exception ex)
        {
            this.LogCreateKeyedWindowFailed(ex, key);
            throw new InvalidOperationException($"Failed to create window with key '{key}'", ex);
        }
    }

    /// <inheritdoc/>
    public async Task<TWindow> CreateDecoratedWindow<TWindow>(WindowCategory category, IReadOnlyDictionary<string, object>? metadata = null)
        where TWindow : Window
    {
        this.LogCreateDecoratedWindow(category, widthMetadata: metadata != null);

        try
        {
            // Create the window using the generic method
            var window = this.container.Resolve<TWindow>();
            _ = await this.windowManagerService.RegisterDecoratedWindowAsync(window, category, metadata).ConfigureAwait(false);

            this.LogDecoratedWindowCreated(category, window.GetType(), widthMetadata: metadata != null);
            return window;
        }
        catch (Exception ex)
        {
            this.LogCreateDecoratedWindowFailed(ex, typeof(TWindow), category.Value);
            throw new InvalidOperationException($"Failed to create decorated window of type {typeof(TWindow).FullName} for category {category}", ex);
        }
    }
}
