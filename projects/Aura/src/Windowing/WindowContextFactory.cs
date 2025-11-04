// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Default implementation of <see cref="IWindowContextFactory"/> that creates window contexts
///     with proper dependency injection.
/// </summary>
/// <remarks>
///     This factory receives dependencies via constructor injection, avoiding the service locator
///     anti-pattern and enabling proper unit testing and DryIoc optimization.
/// </remarks>
public sealed partial class WindowContextFactory : IWindowContextFactory
{
    private readonly ILogger<WindowContextFactory> logger;
    private readonly ILoggerFactory loggerFactory;
    private readonly IEnumerable<IMenuProvider> menuProviders;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowContextFactory"/> class.
    /// </summary>
    /// <param name="logger">The logger for this factory.</param>
    /// <param name="loggerFactory">The logger factory for creating window context loggers.</param>
    /// <param name="menuProviders">The collection of registered menu providers.</param>
    public WindowContextFactory(
        ILogger<WindowContextFactory> logger,
        ILoggerFactory loggerFactory,
        IEnumerable<IMenuProvider> menuProviders)
    {
        this.logger = logger;
        this.loggerFactory = loggerFactory;
        this.menuProviders = menuProviders;
    }

    /// <inheritdoc/>
    public WindowContext Create(
        Window window,
        WindowCategory category,
        Decoration.WindowDecorationOptions? decoration = null,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentNullException.ThrowIfNull(window);

        // Create the window context
        var context = new WindowContext
        {
            Id = window.AppWindow.Id,
            Window = window,
            Category = category,
            CreatedAt = DateTimeOffset.UtcNow,
            Decorations = decoration,
            Metadata = metadata,
        };

        this.LogCreate(context);

        if (decoration is not { Menu.MenuProviderId: { } menuProviderId })
        {
            return context;
        }

        // Resolve menu provider if decoration specifies a menu
        var provider = this.menuProviders.FirstOrDefault(p => string.Equals(
            p.ProviderId,
            menuProviderId,
            StringComparison.Ordinal));

        if (provider is not null)
        {
            context.SetMenuSource(provider.CreateMenuSource());
            this.LogMenuSourceCreated(menuProviderId, context);
        }
        else
        {
            this.LogMenuProviderNotFound(menuProviderId, context);
        }

        return context;
    }
}
