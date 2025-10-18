// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;
using DroidNet.Aura.WindowManagement;
using DroidNet.Config;
using Microsoft.Extensions.DependencyInjection;

namespace DroidNet.Aura;

/// <summary>
/// Extension methods for configuring Aura window management services.
/// </summary>
public static class ServiceCollectionExtensions
{
    /// <summary>
    /// Registers all Aura window management services with optional feature configuration.
    /// </summary>
    /// <param name="services">The service collection to configure.</param>
    /// <param name="configure">Optional configuration action for enabling optional features.</param>
    /// <returns>The service collection for chaining.</returns>
    /// <remarks>
    /// <para>
    /// This method registers the mandatory Aura services and optionally registers additional features
    /// based on the configuration provided.
    /// </para>
    /// <para>
    /// <strong>Mandatory Services (Always Registered):</strong>
    /// <list type="bullet">
    /// <item><description><see cref="IWindowFactory"/> - Factory for creating window instances</description></item>
    /// <item><description><see cref="IWindowContextFactory"/> - Factory for creating window contexts</description></item>
    /// <item><description><see cref="IWindowManagerService"/> - Core window management service</description></item>
    /// </list>
    /// </para>
    /// <para>
    /// <strong>Optional Services (Via Configuration):</strong>
    /// Use the fluent configuration methods on <see cref="AuraOptions"/> to enable optional features.
    /// </para>
    /// <para>
    /// <strong>Menu Providers:</strong>
    /// Menu providers are registered separately using standard DI patterns. Register them as singletons
    /// implementing <see cref="Decoration.IMenuProvider"/> anywhere in your startup code.
    /// </para>
    /// </remarks>
    /// <example>
    /// <strong>Minimal Setup:</strong>
    /// <code>
    /// services.WithAura();
    /// </code>
    /// <para/>
    /// <strong>Full Setup with Optional Features:</strong>
    /// <code>
    /// services.WithAura(options => options
    ///     .WithDecorationSettings()
    ///     .WithAppearanceSettings()
    ///     .WithBackdropService()
    ///     .WithThemeModeService()
    /// );
    /// <para/>
    /// // Register custom windows
    /// services.AddWindow&lt;MainWindow&gt;();
    /// <para/>
    /// // Menu providers registered separately
    /// services.AddSingleton&lt;IMenuProvider&gt;(
    ///     new MenuProvider("App.MainMenu", () => new MenuBuilder()...)
    /// );
    /// </code>
    /// <para/>
    /// <strong>Custom Window Factory:</strong>
    /// <code>
    /// services.WithAura(options => options
    ///     .WithCustomWindowFactory&lt;MyCustomWindowFactory&gt;()
    /// );
    /// </code>
    /// </example>
    public static IServiceCollection WithAura(
        this IServiceCollection services,
        Action<AuraOptions>? configure = null)
    {
        ArgumentNullException.ThrowIfNull(services);

        var options = new AuraOptions();
        configure?.Invoke(options);

        // Register mandatory services
        RegisterMandatoryServices(services, options);

        // Register optional services based on configuration
        RegisterOptionalServices(services, options);

        return services;
    }

    /// <summary>
    /// Registers a window type as transient for creation by the window factory.
    /// </summary>
    /// <typeparam name="TWindow">The window type to register.</typeparam>
    /// <param name="services">The service collection.</param>
    /// <returns>The service collection for chaining.</returns>
    /// <remarks>
    /// Windows should be registered as transient since a new instance is created
    /// each time the window manager requests one.
    /// </remarks>
    public static IServiceCollection AddWindow<TWindow>(this IServiceCollection services)
        where TWindow : Microsoft.UI.Xaml.Window
    {
        ArgumentNullException.ThrowIfNull(services);

        _ = services.AddTransient<TWindow>();

        return services;
    }

    private static void RegisterMandatoryServices(IServiceCollection services, AuraOptions options)
    {
        // Register window factory (default or custom)
        if (options.CustomWindowFactoryType is not null)
        {
            _ = services.AddSingleton(typeof(IWindowFactory), options.CustomWindowFactoryType);
        }
        else
        {
            _ = services.AddSingleton<IWindowFactory, DefaultWindowFactory>();
        }

        // Register window context factory and window manager service
        _ = services.AddSingleton<IWindowContextFactory, WindowContextFactory>();
        _ = services.AddSingleton<IWindowManagerService, WindowManagerService>();
    }

    private static void RegisterOptionalServices(IServiceCollection services, AuraOptions options)
    {
        // Register decoration settings service if requested
        if (options.RegisterDecorationSettings)
        {
            _ = services.AddSingleton<ISettingsService<IWindowDecorationSettings>, WindowDecorationSettingsService>();
        }

        // Register appearance settings service if requested
        if (options.RegisterAppearanceSettings)
        {
            _ = services.AddSingleton<ISettingsService<IAppearanceSettings>, AppearanceSettingsService>();
        }

        // Register backdrop service if requested
        if (options.RegisterBackdropService)
        {
            _ = services.AddSingleton<WindowBackdropService>();
        }

        // Register chrome service if requested
        if (options.RegisterChromeService)
        {
            _ = services.AddSingleton<WindowChromeService>();
        }

        // Register theme mode service if requested
        if (options.RegisterThemeModeService)
        {
            _ = services.AddSingleton<IAppThemeModeService, AppThemeModeService>();
        }
    }
}
