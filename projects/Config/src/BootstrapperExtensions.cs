// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Config.Sources;
using DryIoc;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

/// <summary>
/// Extension methods for integrating the DroidNet Config module with DryIoc dependency injection.
/// </summary>
/// <remarks>
/// <para>
/// These extensions enable easy registration of config sources and strongly-typed settings services in applications using the DroidNet bootstrapper pattern.
/// </para>
/// <para><strong>Typical Usage</strong></para>
/// <code><![CDATA[
/// var bootstrapper = new Bootstrapper(args)
///     .Configure()
///     .WithLoggingAbstraction()
///     .WithConfig()
///     .WithJsonConfigSource(id: "droidnet.aura", filePath: "path/to/Aura.json", watch: true)
///     .WithJsonConfigSource(id: "localsettings", filePath: "path/to/LocalSettings.json", watch: true)
///     .WithSettings<IAppearanceSettings, AppearanceSettingsService>()
///     .WithSettings<IWindowDecorationSettings, WindowDecorationSettingsService>()
///     .Build();
/// ]]></code>
/// <para>
/// This pattern registers the config infrastructure, adds JSON config sources, and enables injection of settings services into client code.
/// </para>
/// </remarks>
public static class BootstrapperExtensions
{
    /// <summary>
    /// Registers baseline Config services in the DryIoc container, including SettingsManager and related abstractions.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The container instance for method chaining.</returns>
    /// <remarks>
    /// Call this before adding config sources or settings services. This sets up the core infrastructure for config and settings management.
    /// </remarks>
    public static IContainer WithConfig(this IContainer container)
    {
        ArgumentNullException.ThrowIfNull(container);

        // Register SettingsManager as singleton
        container.Register<SettingsManager>(
            Reuse.Singleton,
            Made.Of(
                () => new SettingsManager(
                    Arg.Of<IEnumerable<ISettingsSource>>(),
                    Arg.Of<IResolver>(),
                    Arg.Of<ILoggerFactory>())));

        // Map the interface to the concrete singleton instance
        container.RegisterDelegate<ISettingsManager>(
            resolver => resolver.Resolve<SettingsManager>(),
            Reuse.Singleton);

        // Register encryption providers here if needed (future extension)
        _ = 0; // TODO: Placeholder for future encryption provider registrations (concrete class and ifce->concrete)

        return container;
    }

    /// <summary>
    /// Registers a settings service for a specific settings type, enabling injection of strongly-typed settings into client code.
    /// </summary>
    /// <typeparam name="TSettingsInterface">The settings interface type.</typeparam>
    /// <typeparam name="TService">The concrete settings service implementation type (must inherit SettingsService&lt;TSettings&gt;).</typeparam>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The container instance for method chaining.</returns>
    /// <remarks>
    /// Call this for each settings type you want to inject. The service will be resolved from the registered SettingsManager.
    /// </remarks>
    public static IContainer WithSettings<TSettingsInterface, TService>(this IContainer container)
        where TSettingsInterface : class
        where TService : ISettingsService<TSettingsInterface>
    {
        ArgumentNullException.ThrowIfNull(container);

        // Register a concrete service instance `TService` keyed by the settings
        // interface type. This is only for use by the SettingsManager.
        container.Register<ISettingsService<TSettingsInterface>, TService>(Reuse.Singleton, serviceKey: "__uninitialized__");

        // Register a delegate to resolve the consumer-oriented settings service from the SettingsManager
        container.RegisterDelegate(
            resolver =>
            {
                var manager = resolver.Resolve<SettingsManager>();
                return manager.GetService<TSettingsInterface>();
            },
            Reuse.Singleton);

        return container;
    }

    /// <summary>
    /// Registers a JSON config source for settings, optionally with encryption and file watching, keyed by a unique id.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <param name="id">Unique key for the config source (used for service resolution).</param>
    /// <param name="filePath">Path to the JSON settings file.</param>
    /// <param name="encryption">
    /// Optional type of encryption provider. Must implement <see cref="IEncryptionProvider"/> if provided.
    /// </param>
    /// <param name="watch">Whether to watch the file for changes (passed to the source).</param>
    /// <returns>The container instance for method chaining.</returns>
    /// <remarks>
    /// Registers a config source for the given file. Encryption support is planned for future versions.
    /// The <paramref name="id"/> is used as the DryIoc service key.
    /// </remarks>
    public static IContainer WithJsonConfigSource(
        this IContainer container,
        string id,
        string filePath,
        Type? encryption = null,
        bool watch = true)
    {
        ArgumentNullException.ThrowIfNull(container);
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentException.ThrowIfNullOrWhiteSpace(filePath);

        if (encryption != null && !typeof(IEncryptionProvider).IsAssignableFrom(encryption))
        {
            throw new ArgumentException(
                $"The provided type {encryption} must implement {nameof(IEncryptionProvider)}.",
                nameof(encryption));
        }

        container.RegisterDelegate<ISettingsSource>(
            r =>
            {
                var fs = r.Resolve<IFileSystem>();
                var loggerFactory = r.Resolve<ILoggerFactory>();
                var encryptionProvider = encryption == null
                    ? null
                    : (IEncryptionProvider)r.Resolve(encryption);

                return new JsonSettingsSource(id, filePath, fs, watch, encryptionProvider, loggerFactory);
            },
            reuse: Reuse.Singleton,
            serviceKey: id);

        return container;
    }
}
