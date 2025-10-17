// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Default implementation of <see cref="IWindowContextFactory"/> that creates window contexts
/// with proper dependency injection.
/// </summary>
/// <remarks>
/// This factory receives dependencies via constructor injection, avoiding the service locator
/// anti-pattern and enabling proper unit testing and DryIoc optimization.
/// </remarks>
public sealed partial class WindowContextFactory : IWindowContextFactory
{
    private readonly ILogger<WindowContextFactory> logger;
    private readonly ILoggerFactory loggerFactory;
    private readonly IEnumerable<IMenuProvider> menuProviders;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowContextFactory"/> class.
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
        string? title = null,
        Decoration.WindowDecorationOptions? decoration = null,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentNullException.ThrowIfNull(window);

        // Create the window context
        var context = new WindowContext
        {
            Id = Guid.NewGuid(),
            Window = window,
            Category = category,
            Title = title ?? window.Title ?? $"Untitled {category} Window",
            CreatedAt = DateTimeOffset.UtcNow,
            Decoration = decoration,
            Metadata = metadata,
        };

        // Resolve menu provider if decoration specifies a menu
        if (decoration?.Menu is not null)
        {
            var providerId = decoration.Menu.MenuProviderId;

            this.LogCreatingMenuSource(context.Id, providerId);

            var provider = this.menuProviders.FirstOrDefault(p => string.Equals(
                p.ProviderId,
                providerId,
                StringComparison.Ordinal));

            if (provider is not null)
            {
                context.SetMenuSource(provider.CreateMenuSource());
                this.LogMenuSourceCreated(context.Id);
            }
            else
            {
                this.LogMenuProviderNotFound(providerId, context.Id);
            }
        }

        return context;
    }

    [LoggerMessage(
        EventId = 4100,
        Level = LogLevel.Debug,
        Message = "[WindowContextFactory] Creating menu source for window '{WindowId}' using provider '{ProviderId}'")]
    [System.Diagnostics.Conditional("DEBUG")]
    private partial void LogCreatingMenuSource(Guid windowId, string providerId);

    [LoggerMessage(
        EventId = 4101,
        Level = LogLevel.Warning,
        Message = "[WindowContextFactory] Menu provider '{ProviderId}' not found for window '{WindowId}'; window will have no menu")]
    private partial void LogMenuProviderNotFound(string providerId, Guid windowId);

    [LoggerMessage(
        EventId = 4102,
        Level = LogLevel.Information,
        Message = "[WindowContextFactory] Successfully created menu source for window '{WindowId}'")]
    private partial void LogMenuSourceCreated(Guid windowId);
}
