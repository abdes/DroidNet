// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Drag;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Theming;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Resources;
using DryIoc;

namespace DroidNet.Aura;

/// <summary>
///     Extension methods for configuring Aura window management services.
/// </summary>
public static class DependencyInjectionExtensions
{
    /// <summary>
    ///     Registers all Aura window management services with optional feature configuration.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <param name="configure">
    ///     Optional configuration action used to enable optional Aura features. The
    ///     <see cref="AuraOptions"/> fluent API provides options such as
    ///     <see cref="AuraOptions.WithBackdropService"/>, <see cref="AuraOptions.WithDrag"/>, and
    ///     <see cref="AuraOptions.WithThemeModeService"/>.
    /// </param>
    /// <returns>
    ///     The container for chaining.
    /// </returns>
    /// <remarks>
    ///     <para>
    ///     This method registers the mandatory Aura services and optionally registers additional
    ///     features based on the configuration provided.
    ///     </para>
    ///     <para><strong>Mandatory Services (Always Registered):</strong></para>
    ///     <list type="bullet">
    ///     <item><description><see cref="IWindowFactory"/> - Factory for creating window instances</description></item>
    ///     <item><description><see cref="IWindowManagerService"/> - Core window management service</description></item>
    ///     </list>
    ///     <para><strong>Optional Services (Via Configuration):</strong></para>
    ///     <para>
    ///     Use the fluent configuration methods on <see cref="AuraOptions"/> to enable optional features.
    ///     </para>
    ///     <para><strong>Menu Providers:</strong></para>
    ///     <para>
    ///     Menu providers are registered separately using standard DI patterns. Register them as singletons
    ///     implementing <see cref="IMenuProvider"/> anywhere in your startup code.
    ///     </para>
    /// </remarks>
    /// <example>
    ///     <strong>Minimal Setup:</strong>
    ///     <code><![CDATA[
    ///     services.WithAura();
    ///     </code>
    ///     <para><strong>Full Setup with Optional Features:</strong></para>
    ///     <code><![CDATA[
    ///     container.WithAura(options => options
    ///         .WithDecorationSettings()
    ///         .WithAppearanceSettings()
    ///         .WithBackdropService()
    ///         .WithThemeModeService()
    ///         .WithDrag() // registers IDragVisualService and ITabDragCoordinator
    ///     );
    ///
    ///     // Register custom windows
    ///     container.AddWindow&lt;RoutedWindow&gt;();
    ///
    ///     // Menu providers registered separately
    ///     container.Register&lt;IMenuProvider&gt;(
    ///         made: Made.Of(() => new MenuProvider("App.MainMenu", () => new MenuBuilder()...)),
    ///         reuse: Reuse.Singleton
    ///     );
    ///
    ///     // Custom Window Factory
    ///     container.WithAura(options => options
    ///         .WithCustomWindowFactory&lt;MyCustomWindowFactory&gt;()
    ///     );
    ///     ]]></code>
    /// </example>
    public static IContainer WithAura(
        this IContainer container,
        Action<AuraOptions>? configure = null)
    {
        ArgumentNullException.ThrowIfNull(container);

        var options = new AuraOptions();
        configure?.Invoke(options);

        // Register mandatory services
        RegisterMandatoryServices(container, options);

        // Register optional services based on configuration
        RegisterOptionalServices(container, options);

        return container;
    }

    /// <summary>
    ///     Registers a window type as transient for creation by the window factory.
    /// </summary>
    /// <typeparam name="TWindow">The window type to register.</typeparam>
    /// <param name="container">The DryIoc container.</param>
    /// <returns>The container for chaining.</returns>
    /// <remarks>
    ///     Windows should be registered as transient since a new instance is created each time the
    ///     window manager requests one.
    /// </remarks>
    public static IContainer AddWindow<TWindow>(this IContainer container)
        where TWindow : Microsoft.UI.Xaml.Window
    {
        ArgumentNullException.ThrowIfNull(container);

        container.Register<TWindow>(Reuse.Transient);

        return container;
    }

    private static void RegisterMandatoryServices(IContainer container, AuraOptions options)
    {
        // Register the Assets resolver service
        container.Register<AssetResolverService>(Reuse.Singleton);

        // Register window factory (default or custom)
        if (options.CustomWindowFactoryType is not null)
        {
            container.Register(typeof(IWindowFactory), options.CustomWindowFactoryType, Reuse.Singleton);
        }
        else
        {
            container.Register<IWindowFactory, DefaultWindowFactory>(Reuse.Singleton);
        }

        // Register window context factory and window manager service
        container.Register<IWindowManagerService, WindowManagerService>(Reuse.Singleton);

        // Register dialog service (WinUI 3 only)
        container.Register<IDialogService, DialogService>(Reuse.Singleton);
    }

    private static void RegisterOptionalServices(IContainer container, AuraOptions options)
    {
        // Register decoration settings service if requested
        if (options.RegisterDecorationSettings)
        {
            _ = container.WithSettings<IWindowDecorationSettings, WindowDecorationSettingsService>();
        }

        // Register appearance settings service if requested
        if (options.RegisterAppearanceSettings)
        {
            _ = container.WithSettings<IAppearanceSettings, AppearanceSettingsService>();
        }

        // Register backdrop service if requested
        if (options.RegisterBackdropService)
        {
            container.Register<WindowBackdropService>(Reuse.Singleton);
        }

        // Register chrome service if requested
        if (options.RegisterChromeService)
        {
            container.Register<WindowChromeService>(Reuse.Singleton);
        }

        // Register theme mode service if requested
        if (options.RegisterThemeModeService)
        {
            container.Register<IAppThemeModeService, AppThemeModeService>(Reuse.Singleton);
        }

        // Register drag & drop services if requested
        if (options.RegisterDragServices)
        {
            // Drag components are singletons for the application.
            container.Register<IDragVisualService, DragVisualService>(Reuse.Singleton);
            container.Register<ITabDragCoordinator, TabDragCoordinator>(Reuse.Singleton);
        }
    }
}
