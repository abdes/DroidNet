// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Config.Sources;
using DryIoc;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

/// <summary>
/// Extension methods for configuring the Settings Module with DryIoc dependency injection.
/// </summary>
/// <remarks>
/// <para>
/// This extension enables easy integration of the Settings Module into applications built with
/// the DryIoc container and the DroidNet bootstrapper pattern.
/// </para>
/// <para><strong>Service Registration</strong></para>
/// <para>
/// When configuring settings, the following services are automatically registered:
/// </para>
/// <list type="bullet">
///   <item>Settings sources (ISettingsSource implementations) based on file paths</item>
///   <item>Settings manager (ISettingsManager) as a singleton orchestrator</item>
///   <item>Settings service factory (ISettingsService&lt;TSettings&gt;) for typed access</item>
/// </list>
/// <para><strong>Source Type Detection</strong></para>
/// <para>
/// The extension automatically maps file extensions to appropriate source types:
/// </para>
/// <list type="bullet">
///   <item><c>.json</c> - Maps to <see cref="JsonSettingsSource"/> for standard JSON settings</item>
///   <item><c>.secure.json</c> - Reserved for <c>EncryptedJsonSettingsSource</c> (Phase 4, not yet implemented)</item>
/// </list>
/// </remarks>
///
/// <example>
/// <strong>Basic Usage</strong>
/// <code><![CDATA[
/// var bootstrapper = new Bootstrapper(args)
///     .Configure()
///     .WithAppServices(container =>
///     {
///         container
///             .WithSettings(new[]
///             {
///                 "appsettings.json",
///                 "appsettings.Development.json"
///             })
///             .WithSettingsService<AppSettings>()
///             .WithSettingsService<UserPreferences>();
///     })
///     .Build();
/// ]]></code>
///
/// <strong>Using with Path Finder</strong>
/// <code><![CDATA[
/// var bootstrapper = new Bootstrapper(args)
///     .Configure()
///     .WithAppServices(container =>
///     {
///         container.WithSettings(pathFinder =>
///         {
///             return new[]
///             {
///                 pathFinder.GetPath(KnownFolder.ApplicationData, "settings.json"),
///                 pathFinder.GetPath(KnownFolder.LocalApplicationData, "local-settings.json")
///             };
///         })
///         .WithSettingsService<AppSettings>();
///     })
///     .Build();
/// ]]></code>
///
/// <strong>Accessing Settings in Services</strong>
/// <code><![CDATA[
/// public class MyService
/// {
///     private readonly ISettingsService<AppSettings> appSettings;
///
///     public MyService(ISettingsService<AppSettings> appSettings)
///     {
///         this.appSettings = appSettings;
///     }
///
///     public void UseSettings()
///     {
///         var theme = this.appSettings.Current.Theme;
///         this.appSettings.Current.Theme = "Dark";
///         await this.appSettings.SaveAsync();
///     }
/// }
/// ]]></code>
/// </example>
public static class BootstrapperExtensions
{
    /// <summary>
    /// Configures settings services in a DryIoc container with specified file paths.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <param name="settingsFilePaths">The file paths to settings files that will be used as sources.</param>
    /// <returns>The container instance for method chaining.</returns>
    /// <exception cref="ArgumentNullException">
    /// Thrown if <paramref name="container"/> or <paramref name="settingsFilePaths"/> is <see langword="null"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method registers settings sources based on file extensions, creates a settings manager
    /// to orchestrate all sources, and configures a factory for typed settings services.
    /// </para>
    /// <para>
    /// Settings files are loaded in order with a last-loaded-wins strategy. If multiple sources
    /// contain the same setting section, values from later sources override earlier ones.
    /// </para>
    /// </remarks>
    public static IContainer WithSettings(this IContainer container, IEnumerable<string> settingsFilePaths)
    {
        ArgumentNullException.ThrowIfNull(container);
        ArgumentNullException.ThrowIfNull(settingsFilePaths);

        var filePaths = settingsFilePaths.ToList();
        if (filePaths.Count == 0)
        {
            throw new ArgumentException("At least one settings file path must be provided.", nameof(settingsFilePaths));
        }

        // Register settings sources based on file extensions
        RegisterSettingsSources(container, filePaths);

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

        return container;
    }

    /// <summary>
    /// Registers a settings service for a specific settings type.
    /// </summary>
    /// <typeparam name="TSettings">The settings interface type.</typeparam>
    /// <typeparam name="TService">The concrete settings service implementation type.</typeparam>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The container instance for method chaining.</returns>
    /// <exception cref="ArgumentNullException">
    /// Thrown if <paramref name="container"/> is <see langword="null"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method registers the concrete settings service implementation that will be created
    /// by the <see cref="ISettingsManager"/> when <c>GetService&lt;TSettings&gt;()</c> is called.
    /// The application should call this method once for each settings type it needs to use.
    /// </para>
    /// <para>
    /// The settings manager must be registered first by calling <see cref="WithSettings(IContainer, IEnumerable{string})"/>.
    /// </para>
    /// </remarks>
    /// <example>
    /// <code><![CDATA[
    /// container
    ///     .WithSettings(new[] { "appsettings.json" })
    ///     .WithSettingsService<IEditorSettings, EditorSettingsService>()
    ///     .WithSettingsService<IAppSettings, AppSettingsService>();
    /// ]]></code>
    /// </example>
    public static IContainer WithSettingsService<TSettings, TService>(this IContainer container)
        where TSettings : class
        where TService : SettingsService<TSettings>
    {
        ArgumentNullException.ThrowIfNull(container);

        // Register the concrete service implementation using a factory delegate
        container.RegisterDelegate<ISettingsService<TSettings>>(
            resolver =>
            {
                var manager = resolver.Resolve<SettingsManager>();
                var loggerFactory = resolver.Resolve<ILoggerFactory>();
                return (ISettingsService<TSettings>)Activator.CreateInstance(typeof(TService), manager, loggerFactory)!;
            },
            Reuse.Singleton);

        return container;
    }

    /// <summary>
    /// Configures settings services in a DryIoc container using a path resolver function.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <param name="pathResolver">
    /// A function that takes an <see cref="IPathFinder"/> and returns the file paths
    /// to settings files that will be used as sources.
    /// </param>
    /// <returns>The container instance for method chaining.</returns>
    /// <exception cref="ArgumentNullException">
    /// Thrown if <paramref name="container"/> or <paramref name="pathResolver"/> is <see langword="null"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This overload allows you to use the <see cref="IPathFinder"/> service to resolve
    /// application-specific paths for settings files. This is useful when settings need
    /// to be stored in well-known folders like AppData or LocalAppData.
    /// </para>
    /// <para>
    /// The path resolver function is called during container configuration, so the
    /// <see cref="IPathFinder"/> service must already be registered in the container.
    /// </para>
    /// </remarks>
    public static IContainer WithSettings(
        this IContainer container,
        Func<IPathFinder, IEnumerable<string>> pathResolver)
    {
        ArgumentNullException.ThrowIfNull(container);
        ArgumentNullException.ThrowIfNull(pathResolver);

        // Resolve IPathFinder and get file paths
        var pathFinder = container.Resolve<IPathFinder>();
        var filePaths = pathResolver(pathFinder);

        return WithSettings(container, filePaths);
    }

    /// <summary>
    /// Registers settings sources in the container based on file extensions.
    /// </summary>
    /// <param name="container">The DryIoc container to register sources in.</param>
    /// <param name="filePaths">The collection of file paths to create sources for.</param>
    private static void RegisterSettingsSources(IContainer container, List<string> filePaths)
    {
        for (var i = 0; i < filePaths.Count; i++)
        {
            var filePath = filePaths[i];
            var sourceType = GetSourceTypeFromExtension(filePath);

            // Register each source with a unique service key based on index
            var serviceKey = $"SettingsSource_{i}";

            if (sourceType == typeof(JsonSettingsSource))
            {
                // Capture filePath in a closure for the factory
                var capturedFilePath = filePath;

                // Register the source using a factory delegate
                container.RegisterDelegate<ISettingsSource>(
                    resolverContext =>
                    {
                        var fileSystem = resolverContext.Resolve<IFileSystem>();
                        var loggerFactory = resolverContext.Resolve<ILoggerFactory>();
                        return new JsonSettingsSource(capturedFilePath, fileSystem, loggerFactory);
                    },
                    Reuse.Singleton,
                    serviceKey: serviceKey);
            }
            else
            {
                // Future: Handle EncryptedJsonSettingsSource when Phase 4 is implemented
                throw new NotSupportedException(
                    $"Settings source type '{sourceType.Name}' for file '{filePath}' is not yet implemented. " +
                    "Encrypted settings sources will be available in Phase 4.");
            }
        }
    }

    /// <summary>
    /// Determines the appropriate settings source type based on file extension.
    /// </summary>
    /// <param name="filePath">The file path to check.</param>
    /// <returns>The type of settings source to use for this file.</returns>
    /// <remarks>
    /// <para>Supported mappings:</para>
    /// <list type="bullet">
    ///   <item><c>.json</c> → <see cref="JsonSettingsSource"/></item>
    ///   <item><c>.secure.json</c> → <c>EncryptedJsonSettingsSource</c> (Phase 4, not yet implemented)</item>
    /// </list>
    /// </remarks>
    /// <exception cref="ArgumentException">Thrown if the file extension is not recognized.</exception>
    private static Type GetSourceTypeFromExtension(string filePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filePath);

        // Check for .secure.json first (more specific)
        if (filePath.EndsWith(".secure.json", StringComparison.OrdinalIgnoreCase))
        {
            // Return placeholder type - will be implemented in Phase 4
            return typeof(object); // Placeholder for EncryptedJsonSettingsSource
        }

        // Check for .json
        if (filePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
        {
            return typeof(JsonSettingsSource);
        }

        throw new ArgumentException(
            $"Unrecognized settings file extension for '{filePath}'. " +
            "Supported extensions are: .json (for JsonSettingsSource), " +
            ".secure.json (for EncryptedJsonSettingsSource, Phase 4).",
            nameof(filePath));
    }
}
