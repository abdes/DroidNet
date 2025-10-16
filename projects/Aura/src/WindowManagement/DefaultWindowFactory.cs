// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Default implementation of <see cref="IWindowFactory"/> that uses a service provider
/// to resolve window instances with dependency injection.
/// </summary>
public sealed class DefaultWindowFactory : IWindowFactory
{
    private readonly IServiceProvider serviceProvider;
    private readonly ILogger<DefaultWindowFactory> logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="DefaultWindowFactory"/> class.
    /// </summary>
    /// <param name="serviceProvider">The service provider for resolving window instances.</param>
    /// <param name="logger">Logger for diagnostic output.</param>
    public DefaultWindowFactory(IServiceProvider serviceProvider, ILogger<DefaultWindowFactory> logger)
    {
        ArgumentNullException.ThrowIfNull(serviceProvider);
        ArgumentNullException.ThrowIfNull(logger);

        this.serviceProvider = serviceProvider;
        this.logger = logger;
    }

    /// <inheritdoc/>
    public TWindow CreateWindow<TWindow>()
        where TWindow : Window
    {
        try
        {
            this.logger.LogDebug("Resolving window of type {WindowType}", typeof(TWindow).Name);

            var window = this.serviceProvider.GetRequiredService<TWindow>();

            this.logger.LogDebug("Successfully created window of type {WindowType}", typeof(TWindow).Name);

            return window;
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to create window of type {WindowType}", typeof(TWindow).Name);
            throw new InvalidOperationException($"Failed to create window of type {typeof(TWindow).Name}", ex);
        }
    }

    /// <inheritdoc/>
    public Window CreateWindow(string windowTypeName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(windowTypeName);

        try
        {
            this.logger.LogDebug("Resolving window type by name: {TypeName}", windowTypeName);

            var windowType = Type.GetType(windowTypeName)
                ?? throw new ArgumentException($"Type '{windowTypeName}' not found", nameof(windowTypeName));

            if (!typeof(Window).IsAssignableFrom(windowType))
            {
                throw new ArgumentException(
                    $"Type '{windowTypeName}' does not inherit from {nameof(Window)}",
                    nameof(windowTypeName));
            }

            var window = this.serviceProvider.GetRequiredService(windowType) as Window
                ?? throw new InvalidOperationException($"Failed to create window of type {windowTypeName}");

            this.logger.LogDebug("Successfully created window of type {TypeName}", windowTypeName);

            return window;
        }
        catch (Exception ex) when (ex is not ArgumentException)
        {
            this.logger.LogError(ex, "Failed to create window of type {TypeName}", windowTypeName);
            throw new InvalidOperationException($"Failed to create window of type {windowTypeName}", ex);
        }
    }

    /// <inheritdoc/>
    public bool TryCreateWindow<TWindow>(out TWindow? window)
        where TWindow : Window
    {
        window = null;

        try
        {
            window = this.CreateWindow<TWindow>();
            return true;
        }
        catch (Exception ex)
        {
            this.logger.LogWarning(ex, "Failed to create window of type {WindowType}", typeof(TWindow).Name);
            return false;
        }
    }
}
