// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO.Abstractions;
using System.Reflection;
using DroidNet.Config;
using DroidNet.Controls.OutputLog;
using DroidNet.Controls.OutputLog.Theming;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Serilog;
using Serilog.Extensions.Logging;
using Serilog.Templates;
using Testably.Abstractions;

namespace DroidNet.Bootstrap;

/// <summary>
/// Manages the application bootstrapping process for a WinUI application.
/// </summary>
/// <param name="args">Command line arguments from the application entry point.</param>
/// <remarks>
/// The bootstrapping sequence:
/// <list type="number">
/// <item>Basic initialization via <see cref="Configure" /></item>
/// <item>Service configuration through fluent methods like <see cref="WithWinUI{TApplication}" />,
/// <see cref="WithConfiguration" /></item>
/// <item>Application startup with <see cref="Build" /> and <see cref="Run" /></item>
/// </list>
///
/// <para><strong>Customization</strong></para>
/// <para>
/// The bootstrapping process offers flexibility through various customization options. The first
/// opportunity is when <see cref="WithConfiguration"/> isued. You might use the delegates accepted
/// by that method to add configuration files to the host configuration, or to bind configuration
/// sections to strongly-typed options.
/// </para>
/// <para>
/// Another approach is to directly access and configure the `IHostBuilder` for additional customizations.
/// This method is ideal when you need to perform more extensive modifications that go beyond the capabilities
/// of delegates. By working directly with the `IHostBuilder`, you can add or replace services, configure
/// middleware, and set up advanced hosting options. This level of customization is often necessary for
/// complex applications that require a high degree of flexibility and control over the hosting environment.
/// </para>
/// </remarks>
///
/// <example>
/// <strong>Example Usage</strong>
/// <code>
/// var bootstrap = new Bootstrapper(args)
///     .Configure()
///     .WithWinUI()
///     .WithConfiguration(...)
///     .Build();
/// bootstrap.Run();
/// </code>
/// </example>
public sealed partial class Bootstrapper(string[] args) : IDisposable
{
    private bool isDisposed;

    private IContainer? finalContainer;
    private IHost? host;
    private IHostBuilder? builder;

    /// <summary>
    /// Gets the service that resolves application-specific file paths.
    /// </summary>
    /// <remarks>
    /// Must be initialized before any configuration files can be located and loaded.
    /// </remarks>
    public PathFinder? PathFinderService { get; private set; }

    /// <summary>
    /// Gets the abstraction layer for file system operations.
    /// </summary>
    /// <remarks>
    /// Provides testable file system operations and is used by various services including configuration and logging.
    /// </remarks>
    public IFileSystem? FileSystemService { get; private set; }

    /// <summary>
    /// Gets the DryIoc container instance used for dependency injection.
    /// </summary>
    /// <remarks>
    /// The container is only available after the host has been built. Attempting to access this
    /// property before the host is built will throw an <see cref="InvalidOperationException" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if the host has not been built yet.</exception>
    public IContainer Container => this.finalContainer ?? throw new InvalidOperationException("Host not built yet");

    /// <summary>
    /// Performs initial bootstrap configuration including logging, dependency injection, and early services.
    /// </summary>
    /// <returns>The configured bootstrapper instance for method chaining.</returns>
    /// <remarks>
    /// This must be the first method called in the bootstrap chain. It:
    /// <list type="bullet">
    /// <item>Initializes basic logging,</item>
    /// <item>Sets up the DryIoc container,</item>
    /// <item>Configures the pathfinder service,</item>
    /// <item>Prepares early application services.</item>
    /// </list>
    /// </remarks>
    public Bootstrapper Configure()
    {
        this.FileSystemService = new RealFileSystem();

        // Create the early logger, with default config, so we can start logging.
        CreateLogger();

        // This is the initial container that will be used to configure the DryIoc container.
        // It is not the final one and should not be used for anything other than the initial setup.
        var initialContainer = new Container();
        try
        {
            this.builder = Host.CreateDefaultBuilder(args)
                .UseServiceProviderFactory(new DryIocServiceProviderFactory(initialContainer))

                // Add configuration files and configure Options, but first, initialize the IPathFinder instance, so it
                // can be used to resolve configuration file paths.
                .ConfigureAppConfiguration(
                    (_, config) =>
                    {
                        // Build a temporary config to get access to the command line arguments.
                        // NOTE: we expect the `--mode dev|real` optional argument.
                        var tempConfig = config.Build();
                        this.PathFinderService = this.CreatePathFinder(tempConfig);
                    })

                // Continue configuration using DryIoc container API.
                .ConfigureContainer<DryIocServiceProvider>(
                    (context, provider) => this.ConfigureEarlyServices(context, provider.Container));

            initialContainer = null; // Ownership transferred to the HostBuilder
        }
        finally
        {
            initialContainer?.Dispose();
        }

        return this;
    }

    /// <summary>
    /// Configures application settings using JSON configuration files.
    /// </summary>
    /// <param name="getConfigFiles">Function that returns paths to configuration files based on the environment.</param>
    /// <param name="configureOptionsPattern">Action to bind configuration sections to strongly-typed options.</param>
    /// <returns>The bootstrapper instance for method chaining.</returns>
    /// <remarks>
    /// Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    public Bootstrapper WithConfiguration(
        Func<IPathFinder, IFileSystem, IConfiguration, IEnumerable<string>> getConfigFiles,
        Action<IConfiguration, IServiceCollection>? configureOptionsPattern)
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder
            .ConfigureAppConfiguration(
                (context, configBuilder) => this.AddConfigurationFiles(
                    configBuilder,
                    getConfigFiles(this.PathFinderService!, this.FileSystemService!, context.Configuration)));

        if (configureOptionsPattern is not null)
        {
            _ = this.builder.ConfigureServices((context, sc) => configureOptionsPattern(context.Configuration, sc));
        }

        return this;
    }

    /// <summary>
    /// Configures the <see cref="Microsoft.Extensions.Logging" /> abstraction layer as the
    /// preferred logging API for the application.
    /// </summary>
    /// <remarks>
    /// Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    /// </remarks>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    public Bootstrapper WithLoggingAbstraction()
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(provider => ConfigureLogging(provider.Container));
        return this;
    }

    /// <summary>
    /// Configures routing for the application by registering routes and navigation services.
    /// </summary>
    /// <param name="config">The routing configuration containing route definitions and handlers.</param>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <remarks>
    /// Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    public Bootstrapper WithRouting(Routes config)
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(
            provider => provider.Container.ConfigureRouter(config));
        return this;
    }

    /// <summary>
    /// Configures MVVM infrastructure including view location and view-model binding.
    /// </summary>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <remarks>
    /// Registers:
    /// <list type="bullet">
    /// <item>Default view locator for resolving views from view models,</item>
    /// <item>View model to view converter for XAML bindings.</item>
    /// </list>
    /// Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    public Bootstrapper WithMvvm()
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(
            provider =>
            {
                var container = provider.Container;

                // Set up the view model to view converters. We're using the standard converter.
                container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
                container.Register<ViewModelToView>(Reuse.Singleton);
                container.RegisterDelegate((Func<IResolverContext, IValueConverter>)(c => c.Resolve<ViewModelToView>()), serviceKey: "VmToView");
            });
        return this;
    }

    /// <summary>
    /// Initializes WinUI infrastructure.
    /// </summary>
    /// <typeparam name="TApplication">The type of the UI <see cref="Application" />.</typeparam>
    /// <param name="isLifetimeLinked">
    /// Specifies whether the UI lifecycle and the Hosted Application lifecycle are linked or not.
    /// </param>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <remarks>
    /// <list type="bullet">
    /// <item>Adds WinUI hosting services early in the startup sequence,</item>
    /// <item>Links UI thread lifetime with application lifetime,</item>
    /// <item>Must be called after <see cref="Configure" /> but before <see cref="Build" />.</item>
    /// </list>
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    [SuppressMessage("ReSharper", "InconsistentNaming", Justification = "stay consistent with WinUI itself")]
    public Bootstrapper WithWinUI<TApplication>(bool isLifetimeLinked = true)
        where TApplication : Application
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureServices(services => services.ConfigureWinUI<TApplication>(isLifetimeLinked));

        return this;
    }

    /// <summary>
    /// Builds the configured application host and initializes all services.
    /// </summary>
    /// <returns>The built and initialized IHost instance.</returns>
    /// <remarks>
    /// <list type="bullet">
    /// <item>Can only be called once,</item>
    /// <item>Finalizes all service registration,</item>
    /// <item>Creates the service provider,</item>
    /// <item>Initializes all registered services,</item>
    /// <item>Must be called after <see cref="Configure" />, if not, it automatically calls <see cref="Configure" />.</item>
    /// </list>
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if Build has already been called.</exception>
    public IHost Build()
    {
        if (this.host is not null)
        {
            throw new InvalidOperationException("Host already built");
        }

        if (this.builder is null)
        {
            _ = this.Configure();
            Debug.Assert(this.builder is not null, "builder cannot be null after Configure()");
        }

        this.host = this.builder.Build();
        return this.host;
    }

    /// <summary>
    /// Starts the application host and begins processing.
    /// </summary>
    /// <remarks>
    /// <list type="bullet">
    /// <item>Blocks until the application is shut down,</item>
    /// <item>Must be called after <see cref="Build" />, if not, it automatically calls it.</item>
    /// </list>
    /// </remarks>
    public void Run()
    {
        if (this.host is null)
        {
            _ = this.Build();
        }

        Debug.Assert(this.host is not null, "host should be built");
        this.host.Run();
    }

    /// <summary>
    /// Disposes the Bootstrapper instance and releases managed and unmanaged resources.
    /// </summary>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        /* Dispose managed resources */
        this.host?.Dispose();
        this.finalContainer?.Dispose();

        this.FileSystemService = null;
        this.PathFinderService = null;
        this.finalContainer = null;

        /* Dispose unmanaged resources */

        this.isDisposed = true;
    }

    /// <summary>
    /// Configures application-specific services using the IoC container.
    /// <remarks>
    /// This is the preferred and most flexible method, using the <see cref="IRegistrator.Register" />
    /// family of methods.
    /// </remarks>
    /// </summary>
    /// <param name="configureApplicationServices">An action to configure application services.</param>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <seealso cref="WithAppServices(Action{IServiceCollection, Bootstrapper})" />
    public Bootstrapper WithAppServices(Action<IContainer> configureApplicationServices)
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(
            provider => configureApplicationServices(provider.Container));
        return this;
    }

    /// <summary>
    /// Configures application-specific services directly on the <see cref="IServiceCollection" />.
    /// <remarks>
    /// This method is less flexible than <see cref="WithAppServices(Action{IContainer})" />,
    /// but is compatible with those extension methods that use the
    /// <see cref="Microsoft.Extensions.Hosting" /> API.
    /// </remarks>
    /// </summary>
    /// <param name="configureApplicationServices"> An action to configure application services.</param>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <seealso cref="WithAppServices(Action{IContainer})" />
    public Bootstrapper WithAppServices(Action<IServiceCollection, Bootstrapper> configureApplicationServices)
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureServices(sc => configureApplicationServices(sc, this));
        return this;
    }

    /// <summary>
    /// Configures Serilog logging integration with dependency injection.
    /// </summary>
    /// <param name="container">The DryIoc container to register logging services in.</param>
    /// <remarks>
    /// This method:
    /// <list type="number">
    /// <item>Creates and registers a Serilog logger factory</item>
    /// <item>Handles proper disposal of the factory after container ownership transfer</item>
    /// <item>
    /// Registers two logger resolution strategies:
    /// <list type="bullet">
    /// <item>Default logger for components without implementation type,</item>
    /// <item>Type-specific loggers for components with implementation type.</item>
    /// </list>
    /// </item>
    /// </list>
    /// The registration ensures that:
    /// <list type="bullet">
    /// <item>Components get appropriate loggers based on their type,</item>
    /// <item>Logger instances are properly scoped and disposed,</item>
    /// <item>Integration between Serilog and Microsoft.Extensions.Logging is configured.</item>
    /// </list>
    /// </remarks>
    private static void ConfigureLogging(IContainer container)
    {
        var loggerFactory = new SerilogLoggerFactory(Log.Logger);
        try
        {
            container.RegisterInstance<ILoggerFactory>(loggerFactory);
            loggerFactory = null; // Dispose ownership transferred to the DryIoc Container
        }
        finally
        {
            loggerFactory?.Dispose();
        }

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(null!)),
            setup: Setup.With(condition: r => r.Parent.ImplementationType == null));

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(Arg.Index<Type>(0)),
                r => r.Parent.ImplementationType),
            setup: Setup.With(condition: r => r.Parent.ImplementationType != null));
    }

    /// <summary>
    /// Creates a PathFinderConfig instance.
    /// </summary>
    /// <param name="mode">The mode (development or real).</param>
    /// <param name="companyName">The company name.</param>
    /// <param name="applicationName">The application name.</param>
    /// <returns>A PathFinderConfig instance.</returns>
    /// <exception cref="ArgumentNullException">Thrown if companyName or applicationName is null.</exception>
    private static PathFinderConfig CreateFinderConfig(string mode, string? companyName, string? applicationName)
        => new(
            mode,
            companyName ?? throw new ArgumentNullException(nameof(companyName)),
            applicationName ?? throw new ArgumentNullException(applicationName));

    /// <summary>
    /// Gets an assembly attribute of the specified type.
    /// </summary>
    /// <typeparam name="T">The type of the attribute.</typeparam>
    /// <param name="assembly">The assembly.</param>
    /// <returns>The attribute instance, or null if not found.</returns>
    private static T? GetAssemblyAttribute<T>(Assembly assembly)
        where T : Attribute
        => (T?)Attribute.GetCustomAttribute(assembly, typeof(T));

    /// <summary>
    /// Creates and configures the global Serilog logger instance.
    /// </summary>
    /// <param name="container">The IoC container, which can be used to register the OutputLog delegating sink.</param>
    /// <param name="configuration">Optional configuration to customize logger settings. If null, uses default debug configuration.</param>
    /// <remarks>
    /// Configuration behavior:
    /// <list type="number">
    /// <item>
    /// In DEBUG mode:
    /// <list type="bullet">
    /// <item>Uses Debug minimum level,</item>
    /// <item>Writes to Debug output with formatted template,</item>
    /// <item>Format: "[Time Level (SourceContext)] Message\nException",</item>
    /// </list>
    /// </item>
    /// <item>
    /// With provided configuration:
    /// <list type="bullet">
    /// <item>Reads settings from Serilog configuration section,</item>
    /// <item>Supports runtime configuration updates,</item>
    /// </list>
    /// </item>
    /// </list>
    /// The logger is configured differently based on:
    /// <list type="bullet">
    /// <item>Build configuration (DEBUG vs RELEASE),</item>
    /// <item>Application mode (Development vs Real),</item>
    /// <item>Presence of external configuration.</item>
    /// </list>
    /// </remarks>
    /// <seealso cref="ConfigureLogging" />
    private static void CreateLogger(IContainer? container = null, IConfiguration? configuration = null)
    {
        var loggerConfig = new LoggerConfiguration();

#if DEBUG
        var mode = configuration?["mode"] ?? PathFinder.DevelopmentMode;
#else
        var mode = configuration?["mode"] ?? PathFinder.RealMode;
#endif

        if (configuration is null || string.Equals(
                mode,
                PathFinder.DevelopmentMode,
                StringComparison.OrdinalIgnoreCase))
        {
            loggerConfig
                .MinimumLevel.Debug()
                .Enrich.FromLogContext()
                .WriteTo.Debug(
                    new ExpressionTemplate(
                        "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                        new CultureInfo("en-US")));
            if (container is not null)
            {
                loggerConfig.WriteTo.OutputLogView<RichTextBlockSink>(container, theme: OutputLogThemes.Literate);
            }
        }

        if (configuration is not null)
        {
            // https://github.com/serilog/serilog-settings-configuration
            loggerConfig.ReadFrom.Configuration(configuration);
        }

        Log.Logger = loggerConfig.CreateLogger();
    }

    /// <summary>
    /// Validates the bootstrapper state and returns the host builder if valid.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    /// Thrown if the host has already been built,or Configure() hasn't been called yet.
    /// </exception>
    [MemberNotNull(nameof(builder))]
    private void EnsureConfiguredButNotBuilt()
    {
        if (this.host is not null)
        {
            throw new InvalidOperationException("build is already finished");
        }

        if (this.builder is null)
        {
            throw new InvalidOperationException("must call that basic Configure() before you add extensions");
        }
    }

    /// <summary>
    /// Adds configuration files to the IConfigurationBuilder.
    /// This method ensures that configuration files are de-duplicated.
    /// </summary>
    /// <param name="config">The IConfigurationBuilder instance.</param>
    /// <param name="configFiles">The collection of configuration files.</param>
    private void AddConfigurationFiles(IConfigurationBuilder config, IEnumerable<string> configFiles)
    {
        Debug.Assert(this.PathFinderService is not null, "must setup the PathFinderService before adding config files");

        // Always add the appsettings.json file, if it exists.
        _ = config.AddJsonFile(
            this.PathFinderService.GetConfigFilePath("appsettings.json"),
            optional: true,
            reloadOnChange: true);

        // NOTE: Some settings classes may share the same configuration file, and only use a section
        // in it. We should only add the file once.
        foreach (var configFile in new HashSet<string>(configFiles, StringComparer.OrdinalIgnoreCase))
        {
            _ = config.AddJsonFile(configFile, optional: true, reloadOnChange: true);
        }
    }

    /// <summary>
    /// Registers the services created during the early bootstrapping phase in the final container.
    /// </summary>
    /// <param name="context">
    /// The host builder context, which can provide us with the <see cref="IConfiguration" /> object.
    /// </param>
    /// <param name="container">The final container, passed by the host builder.</param>
    private void ConfigureEarlyServices(HostBuilderContext context, IContainer container)
    {
        // This is the final container that will be used by the application.
        this.finalContainer = container;

        Debug.Assert(this.PathFinderService is not null, "did you forget to register the IPathFinder service?");

        container.RegisterInstance(this.FileSystemService);
        container.RegisterInstance<IPathFinder>(this.PathFinderService!);

        CreateLogger(container, context.Configuration);
        Log.Information(
            $"Application `{this.PathFinderService.ApplicationName}` starting in `{this.PathFinderService.Mode}` mode");
    }

    /// <summary>
    /// Creates a PathFinder instance using the specified configuration.
    /// </summary>
    /// <param name="configuration">The configuration.</param>
    /// <returns>A PathFinder instance.</returns>
    /// <exception cref="BootstrapperException">Thrown if the entry assembly is not found.</exception>
    private PathFinder CreatePathFinder(IConfiguration configuration)
    {
#if DEBUG
        var mode = configuration["mode"] ?? PathFinder.DevelopmentMode;
#else
        var mode = configuration["mode"] ?? PathFinder.RealMode;
#endif
        var assembly = Assembly.GetEntryAssembly() ?? throw new BootstrapperException();
        var companyName = GetAssemblyAttribute<AssemblyCompanyAttribute>(assembly)?.Company;
        var applicationName = GetAssemblyAttribute<AssemblyProductAttribute>(assembly)?.Product;

        var finderConfig = CreateFinderConfig(mode, companyName, applicationName);
        return new PathFinder(this.FileSystemService!, finderConfig);
    }
}
