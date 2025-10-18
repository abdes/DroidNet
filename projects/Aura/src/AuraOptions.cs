// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura;

/// <summary>
/// Configuration options for Aura window management services.
/// </summary>
/// <remarks>
/// <para>
/// This class provides a fluent API for configuring optional Aura features during dependency injection setup.
/// Use the <c>WithAura()</c> extension method to register Aura services and configure optional features.
/// </para>
/// <para>
/// <strong>Mandatory Services (Always Registered):</strong>
/// <list type="bullet">
/// <item><description><see cref="WindowManagement.IWindowFactory"/> - Factory for creating window instances</description></item>
/// <item><description><see cref="WindowManagement.IWindowContextFactory"/> - Factory for creating window contexts with menu provider resolution</description></item>
/// <item><description><see cref="WindowManagement.IWindowManagerService"/> - Core window management service</description></item>
/// </list>
/// </para>
/// <para>
/// <strong>Optional Services (Configured via Fluent Methods):</strong>
/// <list type="bullet">
/// <item><description>Window decoration settings - <see cref="WithDecorationSettings"/></description></item>
/// <item><description>Appearance settings - <see cref="WithAppearanceSettings"/></description></item>
/// <item><description>Window backdrop service - <see cref="WithBackdropService"/></description></item>
/// <item><description>Theme mode service - <see cref="WithThemeModeService"/></description></item>
/// <item><description>Custom window factory - <see cref="WithCustomWindowFactory{TFactory}"/></description></item>
/// </list>
/// </para>
/// <para>
/// <strong>Note:</strong> Menu providers are registered separately using standard DI patterns.
/// See the examples below for menu provider registration.
/// </para>
/// </remarks>
/// <example>
/// <strong>Minimal Setup (Mandatory Services Only):</strong>
/// <code>
/// services.WithAura();
/// </code>
///
/// <strong>Full Setup with Optional Features:</strong>
/// <code>
/// services.WithAura(options => options
///     .WithDecorationSettings()
///     .WithAppearanceSettings()
///     .WithBackdropService()
///     .WithThemeModeService()
/// );
///
/// // Register custom windows
/// services.AddWindow&lt;MainWindow&gt;();
/// services.AddWindow&lt;ToolWindow&gt;();
///
/// // Menu providers registered separately
/// services.AddSingleton&lt;IMenuProvider&gt;(
///     new MenuProvider("App.MainMenu", () => new MenuBuilder()
///         .AddItem("File", cmd => fileMenuBuilder)
///         .AddItem("Edit", cmd => editMenuBuilder))
/// );
/// </code>
///
/// <strong>Custom Window Factory:</strong>
/// <code>
/// services.WithAura(options => options
///     .WithCustomWindowFactory&lt;MyCustomWindowFactory&gt;()
///     .WithDecorationSettings()
/// );
/// </code>
/// </example>
public sealed class AuraOptions
{
    /// <summary>
    /// Gets a value indicating whether window decoration settings should be registered.
    /// </summary>
    internal bool RegisterDecorationSettings { get; private set; }

    /// <summary>
    /// Gets a value indicating whether appearance settings should be registered.
    /// </summary>
    internal bool RegisterAppearanceSettings { get; private set; }

    /// <summary>
    /// Gets a value indicating whether the window backdrop service should be registered.
    /// </summary>
    internal bool RegisterBackdropService { get; private set; }

    /// <summary>
    /// Gets a value indicating whether the theme mode service should be registered.
    /// </summary>
    internal bool RegisterThemeModeService { get; private set; }

    /// <summary>
    /// Gets the custom window factory type, if specified.
    /// </summary>
    internal Type? CustomWindowFactoryType { get; private set; }

    /// <summary>
    /// Registers the window decoration settings service (<see cref="Config.ISettingsService{T}"/>
    /// wrapping <see cref="Decoration.WindowDecorationSettings"/>).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    /// This enables persistent window decoration preferences including category-specific overrides
    /// and code-defined defaults. The service is registered as a singleton.
    /// </remarks>
    public AuraOptions WithDecorationSettings()
    {
        this.RegisterDecorationSettings = true;
        return this;
    }

    /// <summary>
    /// Registers the appearance settings service (<see cref="Config.ISettingsService{T}"/>
    /// wrapping <see cref="IAppearanceSettings"/>).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    /// This enables application-wide appearance preferences including theme mode, background color,
    /// and font family. The service is registered as a singleton following the Config module pattern.
    /// </remarks>
    public AuraOptions WithAppearanceSettings()
    {
        this.RegisterAppearanceSettings = true;
        return this;
    }

    /// <summary>
    /// Registers the window backdrop service for applying backdrop effects (Mica, Acrylic, etc.).
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    /// This enables backdrop application based on window decoration options. The backdrop service
    /// coordinates backdrop effects with window-specific overrides. The service is registered as a singleton.
    /// </remarks>
    public AuraOptions WithBackdropService()
    {
        this.RegisterBackdropService = true;
        return this;
    }

    /// <summary>
    /// Registers the theme mode service for applying Light/Dark/System themes to windows.
    /// </summary>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    /// This enables automatic theme application to windows based on appearance settings.
    /// The service is registered as a singleton.
    /// </remarks>
    public AuraOptions WithThemeModeService()
    {
        this.RegisterThemeModeService = true;
        return this;
    }

    /// <summary>
    /// Registers a custom window factory implementation.
    /// </summary>
    /// <typeparam name="TFactory">The custom window factory type implementing <see cref="WindowManagement.IWindowFactory"/>.</typeparam>
    /// <returns>This <see cref="AuraOptions"/> instance for method chaining.</returns>
    /// <remarks>
    /// Use this method to replace the default window factory with a custom implementation.
    /// The custom factory must implement <see cref="WindowManagement.IWindowFactory"/> and will be
    /// registered as a singleton.
    /// </remarks>
    /// <example>
    /// <code>
    /// services.WithAura(options => options
    ///     .WithCustomWindowFactory&lt;MyCustomWindowFactory&gt;()
    /// );
    /// </code>
    /// </example>
    public AuraOptions WithCustomWindowFactory<TFactory>()
        where TFactory : class, WindowManagement.IWindowFactory
    {
        this.CustomWindowFactoryType = typeof(TFactory);
        return this;
    }
}
