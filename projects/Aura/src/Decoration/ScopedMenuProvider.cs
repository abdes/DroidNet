// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Menus;
using Microsoft.Extensions.DependencyInjection;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Menu provider that resolves dependencies from the DI container when creating menu sources.
/// </summary>
/// <remarks>
/// <para>
/// This provider is suitable for menus that require services from the dependency injection
/// container. The configuration action receives a <see cref="MenuBuilder"/> and an
/// <see cref="IServiceProvider"/> allowing you to resolve services and build the menu
/// dynamically.
/// </para>
/// <para>
/// Unlike <see cref="MenuProvider"/>, which uses a simple factory function, this provider
/// integrates with the DI container to provide access to registered services such as
/// command handlers, view models, or application state.
/// </para>
/// <para>
/// The provider is thread-safe and can handle concurrent menu source creation requests.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// // Register with service-dependent menu items
/// var provider = new ScopedMenuProvider(
///     "App.MainMenu",
///     serviceProvider,
///     (builder, sp) =>
///     {
///         var commandService = sp.GetRequiredService&lt;ICommandService&gt;();
///         var settings = sp.GetRequiredService&lt;IAppSettings&gt;();
///
///         builder.AddMenuItem("New", commandService.NewCommand, null, "Ctrl+N");
///         builder.AddMenuItem("Open", commandService.OpenCommand, null, "Ctrl+O");
///
///         if (settings.ShowAdvancedFeatures)
///         {
///             builder.AddMenuItem("Advanced", commandService.AdvancedCommand);
///         }
///     });
///
/// // Register in DI
/// services.AddSingleton&lt;IMenuProvider&gt;(provider);
/// </code>
/// </example>
/// <seealso cref="IMenuProvider"/>
/// <seealso cref="MenuProvider"/>
public sealed class ScopedMenuProvider : IMenuProvider
{
    private readonly IServiceProvider serviceProvider;
    private readonly Action<MenuBuilder, IServiceProvider> configureMenu;
    private readonly Lock lockObject = new();

    /// <summary>
    /// Initializes a new instance of the <see cref="ScopedMenuProvider"/> class.
    /// </summary>
    /// <param name="providerId">
    /// The unique identifier for this menu provider. Must be non-empty.
    /// </param>
    /// <param name="serviceProvider">
    /// The service provider used to resolve dependencies when building menus.
    /// </param>
    /// <param name="configureMenu">
    /// An action that configures the menu using the provided <see cref="MenuBuilder"/>
    /// and <see cref="IServiceProvider"/>. This action is invoked each time
    /// <see cref="CreateMenuSource"/> is called.
    /// </param>
    /// <exception cref="ArgumentException">
    /// Thrown if <paramref name="providerId"/> is null, empty, or whitespace.
    /// </exception>
    /// <exception cref="ArgumentNullException">
    /// Thrown if <paramref name="serviceProvider"/> or <paramref name="configureMenu"/>
    /// is <see langword="null"/>.
    /// </exception>
    public ScopedMenuProvider(
        string providerId,
        IServiceProvider serviceProvider,
        Action<MenuBuilder, IServiceProvider> configureMenu)
    {
        if (string.IsNullOrWhiteSpace(providerId))
        {
            throw new ArgumentException("Provider ID must be a non-empty string.", nameof(providerId));
        }

        this.ProviderId = providerId;
        this.serviceProvider = serviceProvider ?? throw new ArgumentNullException(nameof(serviceProvider));
        this.configureMenu = configureMenu ?? throw new ArgumentNullException(nameof(configureMenu));
    }

    /// <inheritdoc/>
    public string ProviderId { get; }

    /// <inheritdoc/>
    /// <remarks>
    /// <para>
    /// This method creates a new <see cref="MenuBuilder"/> instance, optionally resolving
    /// it from the DI container if registered, and then invokes the configuration action
    /// with the builder and service provider. The resulting menu source is built and returned.
    /// </para>
    /// <para>
    /// This method is thread-safe and can be called concurrently from multiple threads.
    /// A lock is used to ensure that menu building operations are serialized to prevent
    /// potential data races.
    /// </para>
    /// </remarks>
    /// <exception cref="InvalidOperationException">
    /// Thrown if required services cannot be resolved from the service provider.
    /// </exception>
    public IMenuSource CreateMenuSource()
    {
        lock (this.lockObject)
        {
            // Try to resolve MenuBuilder from DI (may have ILoggerFactory dependency)
            // If not registered, create a default instance
            var builder = this.serviceProvider.GetService<MenuBuilder>() ?? new MenuBuilder();

            this.configureMenu(builder, this.serviceProvider);

            return builder.Build();
        }
    }
}
