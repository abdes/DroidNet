// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Default implementation of <see cref="IWindowFactory"/> that uses a service provider
/// to resolve window instances with dependency injection.
/// </summary>
public sealed partial class DefaultWindowFactory : IWindowFactory
{
    private readonly IServiceProvider serviceProvider;
    private readonly ILogger<DefaultWindowFactory> logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="DefaultWindowFactory"/> class.
    /// </summary>
    /// <param name="serviceProvider">The service provider for resolving window instances.</param>
    /// <param name="loggerFactory">Optional logger factory used to create a service logger.</param>
    public DefaultWindowFactory(IServiceProvider serviceProvider, ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(serviceProvider);

        this.serviceProvider = serviceProvider;
        this.logger = loggerFactory?.CreateLogger<DefaultWindowFactory>() ?? NullLogger<DefaultWindowFactory>.Instance;
    }

    /// <inheritdoc/>
    public TWindow CreateWindow<TWindow>()
        where TWindow : Window
    {
        try
        {
            this.LogResolvingWindow(typeof(TWindow).Name);

            var window = this.serviceProvider.GetRequiredService<TWindow>();

            this.LogResolvedWindow(typeof(TWindow).Name);

            return window;
        }
        catch (Exception ex)
        {
            this.LogCreateWindowFailed(ex, typeof(TWindow).Name);
            throw new InvalidOperationException($"Failed to create window of type {typeof(TWindow).Name}", ex);
        }
    }

    /// <inheritdoc/>
    public Window CreateWindow(string category)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(category);

        try
        {
            this.LogResolvingWindowByName(category);

            var windowType = Type.GetType(category)
                ?? throw new ArgumentException($"Type '{category}' not found", nameof(category));

            if (!typeof(Window).IsAssignableFrom(windowType))
            {
                throw new ArgumentException(
                    $"Type '{category}' does not inherit from {nameof(Window)}",
                    nameof(category));
            }

            var window = this.serviceProvider.GetRequiredService(windowType) as Window
                ?? throw new InvalidOperationException($"Failed to create window of type {category}");

            this.LogResolvedWindowByName(category);

            return window;
        }
        catch (Exception ex) when (ex is not ArgumentException)
        {
            this.LogCreateWindowByNameFailed(ex, category);
            throw new InvalidOperationException($"Failed to create window of type {category}", ex);
        }
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Window creation failures are logged and reported via false return value")]
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
            this.LogTryCreateWindowFailed(ex, typeof(TWindow).Name);
            return false;
        }
    }
}
