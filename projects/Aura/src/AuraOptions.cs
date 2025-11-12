// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura;

/// <summary>
///     Configuration options for Aura window management services.
/// </summary>
/// <remarks>
///     This class provides a fluent API for configuring optional Aura features during dependency
///     injection setup. Use the <c>WithAura()</c> extension method to register Aura services and
///     configure optional features.
/// </remarks>
public sealed class AuraOptions
{
    /// <summary>
    ///     Gets a value indicating whether window decoration settings should be registered.
    /// </summary>
    internal bool RegisterDecorationSettings { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether appearance settings should be registered.
    /// </summary>
    internal bool RegisterAppearanceSettings { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether the window backdrop service should be registered.
    /// </summary>
    internal bool RegisterBackdropService { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether the window chrome service should be registered.
    /// </summary>
    internal bool RegisterChromeService { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether the theme mode service should be registered.
    /// </summary>
    internal bool RegisterThemeModeService { get; private set; }

    /// <summary>
    ///     Gets the custom window factory type, if specified.
    /// </summary>
    internal Type? CustomWindowFactoryType { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether drag &amp; drop services should be registered.
    /// </summary>
    internal bool RegisterDragServices { get; private set; }

    /// <summary>
    ///     Registers the window decoration settings service (<see
    ///     cref="Config.ISettingsService{T}"/> wrapping <see cref="WindowDecorationSettings"/>).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    ///     This enables persistent window decoration preferences including category-specific
    ///     overrides and code-defined defaults. The service is registered as a singleton.
    /// </remarks>
    public AuraOptions WithDecorationSettings()
    {
        this.RegisterDecorationSettings = true;
        return this;
    }

    /// <summary>
    ///     Registers the appearance settings service (<see cref="Config.ISettingsService{T}"/>
    ///     wrapping <see cref="IAppearanceSettings"/>).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    ///     This enables application-wide appearance preferences including theme mode, background
    ///     color, and font family. The service is registered as a singleton following the Config
    ///     module pattern.
    /// </remarks>
    public AuraOptions WithAppearanceSettings()
    {
        this.RegisterAppearanceSettings = true;
        return this;
    }

    /// <summary>
    ///     Registers the window backdrop service for applying backdrop effects (Mica, Acrylic,
    ///     etc.).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    ///     This enables backdrop application based on window decoration options. The backdrop
    ///     service coordinates backdrop effects with window-specific overrides. The service is
    ///     registered as a singleton.
    /// </remarks>
    public AuraOptions WithBackdropService()
    {
        this.RegisterBackdropService = true;
        return this;
    }

    /// <summary>
    ///     Registers the window chrome service for applying chrome decorations
    ///     (ExtendsContentIntoTitleBar, etc.).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    ///     This enables chrome application based on window decoration options. The chrome service
    ///     configures title bar customization and system chrome settings. The service is registered
    ///     as a singleton.
    /// </remarks>
    public AuraOptions WithChromeService()
    {
        this.RegisterChromeService = true;
        return this;
    }

    /// <summary>
    ///     Registers the theme mode service for applying Light/Dark/System themes to windows.
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    ///     This enables automatic theme application to windows based on appearance settings. The
    ///     service is registered as a singleton.
    /// </remarks>
    public AuraOptions WithThemeModeService()
    {
        this.RegisterThemeModeService = true;
        return this;
    }

    /// <summary>
    ///     Registers drag &amp; drop services (TabDragCoordinator and DragVisualService).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    public AuraOptions WithDrag()
    {
        this.RegisterDragServices = true;
        return this;
    }

    /// <summary>
    ///     Registers a custom window factory implementation.
    /// </summary>
    /// <typeparam name="TFactory">The custom window factory type implementing <see cref="IWindowFactory"/>.</typeparam>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    ///     Use this method to replace the default window factory with a custom implementation. The
    ///     custom factory must implement <see cref="IWindowFactory"/> and will be registered as a
    ///     singleton.
    /// </remarks>
    /// <example>
    ///     <code><![CDATA[
    ///     services.WithAura(options => options
    ///         .WithCustomWindowFactory&lt;MyCustomWindowFactory&gt;()
    ///     );
    ///     ]]></code>
    /// </example>
    public AuraOptions WithCustomWindowFactory<TFactory>()
        where TFactory : class, IWindowFactory
    {
        this.CustomWindowFactoryType = typeof(TFactory);
        return this;
    }
}
