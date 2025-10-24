// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO.Abstractions;
using System.Reflection;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Serilog;
using Serilog.Configuration;
using Serilog.Core;
using Serilog.Events;
using Serilog.Extensions.Logging;
using Serilog.Templates;
using Testably.Abstractions;

namespace DroidNet.Bootstrap;

/// <summary>
///     Manages the application bootstrapping process for a WinUI application.
/// </summary>
/// <param name="args">Command line arguments from the application entry point.</param>
/// <remarks>
///     The bootstrapping sequence:
///     <list type="number">
///         <item>Basic initialization via <see cref="Configure" /></item>
///         <item>
///             Service configuration through fluent methods like <see cref="WithWinUI{TApplication}" />,
///             <see cref="WithMvvm" />, and <see cref="WithRouting" />
///         </item>
///         <item>Application startup with <see cref="Build" /> and <see cref="Run" /></item>
///     </list>
///     <para>
///         <strong>Customization</strong>
///     </para>
///     <para>
///         The bootstrapping process offers flexibility through various customization options. You can
///         configure bootstrap options by passing an <see cref="Action{BootstrapOptions}" /> to the
///         <see cref="Configure" /> method to control logging outputs, format providers, and other
///         bootstrap-time settings.
///     </para>
///     <para>
///         For more advanced customization, you can directly access the underlying `IHostBuilder` instance
///         through the builder field after calling <see cref="Configure" />. This allows you to add or replace
///         services, configure middleware, and set up advanced hosting options. This level of customization is
///         often necessary for complex applications that require a high degree of flexibility and control over
///         the hosting environment.
///     </para>
/// </remarks>
/// <example>
///     <strong>Example Usage</strong>
///     <code><![CDATA[
///     var bootstrap = new Bootstrapper(args)
///     .Configure(options => options
///         .WithOutputConsole()
///         .WithOutputLog())
///     .WithMvvm()
///     .WithRouting(routes)
///     .WithWinUI&lt;App&gt;()
///     .Build();
///     bootstrap.Run();
/// ]]></code>
/// </example>
public sealed partial class Bootstrapper(string[] args) : IDisposable
{
    private IHostBuilder? builder;

    private IContainer? finalContainer;
    private IHost? host;
    private bool isDisposed;

    /// <summary>
    ///     Gets the service that resolves application-specific file paths.
    /// </summary>
    /// <remarks>
    ///     Must be initialized before any configuration files can be located and loaded.
    /// </remarks>
    public PathFinder? PathFinderService { get; private set; }

    /// <summary>
    ///     Gets the abstraction layer for file system operations.
    /// </summary>
    /// <remarks>
    ///     Provides testable file system operations and is used by various services including configuration and logging.
    /// </remarks>
    public IFileSystem? FileSystemService { get; private set; }

    /// <summary>
    ///     Gets the DryIoc container instance used for dependency injection.
    /// </summary>
    /// <remarks>
    ///     The container is only available after the host has been built. Attempting to access this
    ///     property before the host is built will throw an <see cref="InvalidOperationException" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if the host has not been built yet.</exception>
    public IContainer Container => this.finalContainer ?? throw new InvalidOperationException("Host not built yet");

    /// <summary>
    ///     Disposes the Bootstrapper instance and releases managed and unmanaged resources.
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
    ///     Performs initial bootstrap configuration including logging, dependency injection, and early services.
    /// </summary>
    /// <param name="configure">
    ///     Optional delegate to configure <see cref="BootstrapOptions" />. Use this to control logging sinks
    ///     (OutputConsole, OutputLog), format providers, log levels, and other bootstrap-time settings.
    /// </param>
    /// <returns>The configured bootstrapper instance for method chaining.</returns>
    /// <remarks>
    ///     This must be the first method called in the bootstrap chain. It:
    ///     <list type="bullet">
    ///         <item>Initializes the file system abstraction,</item>
    ///         <item>Sets up the DryIoc container,</item>
    ///         <item>Configures the pathfinder service,</item>
    ///         <item>Initializes basic Serilog logging,</item>
    ///         <item>Prepares early application services.</item>
    ///     </list>
    /// </remarks>
    public Bootstrapper Configure(Action<BootstrapOptions>? configure = null)
    {
        var options = new BootstrapOptions();
        configure?.Invoke(options);
        this.LogConfigureCalled();

        this.FileSystemService = new RealFileSystem();

        // This is the initial container that will be used to configure the DryIoc container.
        // It is not the final one and should not be used for anything other than the initial setup.
        var initialContainer = new Container();
        try
        {
            this.builder = Host.CreateDefaultBuilder(args)
                .UseServiceProviderFactory(new DryIocServiceProviderFactory(initialContainer))

                // Clear default logging providers - we'll use Serilog exclusively
                .ConfigureLogging(logging => logging.ClearProviders())

                // Add configuration files and configure Options, but first, initialize the IPathFinder instance, so it
                // can be used to resolve configuration file paths.
                .ConfigureAppConfiguration((_, config) =>
                {
                    // Build a temporary config to get access to the command line arguments.
                    // NOTE: we expect the `--mode dev|real` optional argument.
                    var tempConfig = config.Build();
                    this.PathFinderService = this.CreatePathFinder(tempConfig);

                    // Create the early logger, with default config, so we can start logging.
                    this.CreateLogger();

                    Log.Information(
                        $"Application `{this.PathFinderService.ApplicationName}` starting in `{this.PathFinderService.Mode}` mode");
                })

                // Configure Serilog as the logging provider for Microsoft.Extensions.Logging
                .UseSerilog()

                // Continue configuration using DryIoc container API.
                .ConfigureContainer<DryIocServiceProvider>((_, provider) =>
                    this.ConfigureEarlyServices(provider.Container, options));

            initialContainer = null; // Ownership transferred to the HostBuilder
        }
        finally
        {
            initialContainer?.Dispose();
        }

        return this;
    }

    /// <summary>
    ///     Configures routing for the application by registering routes and navigation services.
    /// </summary>
    /// <param name="config">The routing configuration containing route definitions and handlers.</param>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <remarks>
    ///     Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    public Bootstrapper WithRouting(Routes config)
    {
        this.EnsureConfiguredButNotBuilt();

        this.LogWithRouting();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(provider =>
            provider.Container.ConfigureRouter(config));
        return this;
    }

    /// <summary>
    ///     Configures MVVM infrastructure including view location and view-model binding.
    /// </summary>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <remarks>
    ///     Registers:
    ///     <list type="bullet">
    ///         <item>Default view locator for resolving views from view models,</item>
    ///         <item>View model to view converter for XAML bindings.</item>
    ///     </list>
    ///     Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    public Bootstrapper WithMvvm()
    {
        this.EnsureConfiguredButNotBuilt();
        this.LogWithMvvm();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(provider =>
        {
            var container = provider.Container;

            // Set up the view model to view converters. We're using the standard converter.
            container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
            container.Register<ViewModelToView>(Reuse.Singleton);
            container.RegisterDelegate(
                (Func<IResolverContext, IValueConverter>)(c => c.Resolve<ViewModelToView>()),
                serviceKey: "VmToView");
        });
        return this;
    }

    /// <summary>
    ///     Configures WinUI infrastructure and registers the application instance.
    /// </summary>
    /// <typeparam name="TApplication">The type of the WinUI <see cref="Application" />.</typeparam>
    /// <param name="isLifetimeLinked">
    ///     Specifies whether the UI lifecycle and the Hosted Application lifecycle are linked.
    ///     When <see langword="true"/> (default), closing the UI will terminate the application, and vice versa.
    ///     When <see langword="false"/>, the UI and application lifetimes are independent.
    /// </param>
    /// <returns>The Bootstrapper instance for chaining calls.</returns>
    /// <remarks>
    ///     <list type="bullet">
    ///         <item>Registers WinUI hosting services,</item>
    ///         <item>Configures the UI thread and application lifecycle,</item>
    ///         <item>Must be called after <see cref="Configure" /> but before <see cref="Build" />.</item>
    ///     </list>
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if called after Build().</exception>
    [SuppressMessage("ReSharper", "InconsistentNaming", Justification = "stay consistent with WinUI itself")]
    public Bootstrapper WithWinUI<TApplication>(bool isLifetimeLinked = true)
        where TApplication : Application
    {
        this.EnsureConfiguredButNotBuilt();

        this.LogWithWinUI<TApplication>(isLifetimeLinked);

        _ = this.builder.ConfigureServices(services => services.ConfigureWinUI<TApplication>(isLifetimeLinked));

        return this;
    }

    /// <summary>
    ///     Builds the configured application host and initializes all services.
    /// </summary>
    /// <returns>The built and initialized IHost instance.</returns>
    /// <remarks>
    ///     <list type="bullet">
    ///         <item>Can only be called once,</item>
    ///         <item>Finalizes all service registration,</item>
    ///         <item>Creates the service provider,</item>
    ///         <item>Initializes all registered services,</item>
    ///         <item>Must be called after <see cref="Configure" />, if not, it automatically calls <see cref="Configure" />.</item>
    ///     </list>
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

        // Try to obtain the final ILogger for Bootstrapper from the host's service provider.
        try
        {
            var resolved = this.host.Services.GetService(typeof(ILogger<Bootstrapper>)) as Microsoft.Extensions.Logging.ILogger;
            if (resolved is not null)
            {
                this.logger = resolved;
                this.LogHostBuilt();
            }
            else
            {
                Log.Information("Host built but final ILogger<Bootstrapper> not available");
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            Log.Warning(ex, "Failed to obtain ILogger<Bootstrapper> from host.Services");
        }
#pragma warning restore CA1031 // Do not catch general exception types

        return this.host;
    }

    /// <summary>
    ///     Starts the application host and begins processing.
    /// </summary>
    /// <remarks>
    ///     <list type="bullet">
    ///         <item>Blocks until the application is shut down,</item>
    ///         <item>Must be called after <see cref="Build" />, if not, it automatically calls it.</item>
    ///     </list>
    /// </remarks>
    public void Run()
    {
        if (this.host is null)
        {
            _ = this.Build();
        }

        Debug.Assert(this.host is not null, "host should be built");

        // Log the start of the host run cycle (uses final ILogger when available)
        this.LogHostStarting();

        try
        {
            this.host.Run();
        }
        finally
        {
            // host.Run returns when the application is shutting down — log finalization
            this.LogHostStopped();
        }
    }

    /// <summary>
    ///     Creates a PathFinderConfig instance.
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
    ///     Gets an assembly attribute of the specified type.
    /// </summary>
    /// <typeparam name="T">The type of the attribute.</typeparam>
    /// <param name="assembly">The assembly.</param>
    /// <returns>The attribute instance, or null if not found.</returns>
    private static T? GetAssemblyAttribute<T>(Assembly assembly)
        where T : Attribute
        => (T?)Attribute.GetCustomAttribute(assembly, typeof(T));

    private static void ConfigureLoggingControls(LoggerConfiguration loggerConfig, DryIoc.IContainer container, BootstrapOptions? options)
    {
        if (options is null)
        {
            return;
        }

        try
        {
            if (options.RegisterOutputLog)
            {
                ConfigureOutputLog(loggerConfig, container, options);
            }

            if (options.RegisterOutputConsole)
            {
                ConfigureOutputConsole(loggerConfig, container, options);
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch
        {
            // Optional sinks are best-effort. Swallow any exception to avoid crashing the host bootstrap.
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    [SuppressMessage("Design", "MA0051:Method is too long", Justification = "method is quite simple, but some calls have many arguments")]
    private static void ConfigureOutputLog(LoggerConfiguration loggerConfig, IContainer container, BootstrapOptions options)
    {
        var assembly = TryLoadAssembly("DroidNet.Controls.OutputLog");
        if (assembly is null)
        {
            return;
        }

        var extType = assembly.GetType(
            "DroidNet.Controls.OutputLog.LoggerSinkConfigurationOutputLogExtensions",
            throwOnError: false);

        if (extType is null)
        {
            return;
        }

        var sinkType = assembly.GetType("DroidNet.Controls.OutputLog.RichTextBlockSink", throwOnError: false);
        if (sinkType is null)
        {
            return;
        }

        try
        {
            var writeTo = GetWriteToConfiguration(loggerConfig);
            if (writeTo is null)
            {
                return;
            }

            var method = extType.GetMethod(
                "OutputLogView",
                BindingFlags.Public | BindingFlags.Static,
                binder: null,
                [
                    typeof(LoggerSinkConfiguration),
                    typeof(IContainer),
                    typeof(string),
                    typeof(IFormatProvider),
                    typeof(LogEventLevel),
                    typeof(LoggingLevelSwitch),
                    typeof(uint?),
                ],
                modifiers: null);

            if (method is not null)
            {
                var genericMethod = method.MakeGenericMethod(sinkType);
                _ = genericMethod.Invoke(
                    null,
                    [
                        writeTo,
                        container,
                        options.OutputLogTemplate,
                        options.OutputLogFormatProvider,
                        options.OutputLogMinimumLevel,
                        options.OutputLogLevelSwitch,
                        options.OutputLogThemeId,
                    ]);
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch
        {
            // best-effort; ignore plugin failures
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    private static void ConfigureOutputConsole(LoggerConfiguration loggerConfig, IContainer container, BootstrapOptions options)
    {
        var assembly = TryLoadAssembly("DroidNet.Controls.OutputConsole");
        if (assembly is null)
        {
            return;
        }

        var extType = assembly.GetType(
            "DroidNet.Controls.OutputConsole.LoggerSinkConfigurationOutputConsoleExtensions",
            throwOnError: false);

        if (extType is null)
        {
            return;
        }

        try
        {
            var writeTo = GetWriteToConfiguration(loggerConfig);
            if (writeTo is null)
            {
                return;
            }

            var method = extType.GetMethod(
                "OutputConsole",
                BindingFlags.Public | BindingFlags.Static,
                binder: null,
                [
                    typeof(LoggerSinkConfiguration),
                    typeof(IContainer),
                    typeof(int),
                    typeof(LogEventLevel),
                    typeof(LoggingLevelSwitch),
                ],
                modifiers: null);

            if (method is not null)
            {
                _ = method.Invoke(
                    null,
                    [
                        writeTo,
                        container,
                        options.OutputConsoleCapacity,
                        options.OutputConsoleMinimumLevel,
                        options.OutputConsoleLevelSwitch,
                    ]);
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch
        {
            // best-effort; ignore plugin failures
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    private static Assembly? TryLoadAssembly(string assemblyName)
    {
        // Try to find the assembly in already loaded assemblies first
        var assembly = AppDomain.CurrentDomain.GetAssemblies()
            .FirstOrDefault(a => string.Equals(a.GetName().Name, assemblyName, StringComparison.OrdinalIgnoreCase));

        // If not loaded, attempt to load by name (may throw if not present)
        if (assembly is null)
        {
            try
            {
                assembly = Assembly.Load(new AssemblyName(assemblyName));
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch
            {
                // Assembly not available, return null
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }

        return assembly;
    }

    private static LoggerSinkConfiguration? GetWriteToConfiguration(LoggerConfiguration loggerConfig)
    {
        var writeToProp = typeof(LoggerConfiguration).GetProperty("WriteTo", BindingFlags.Public | BindingFlags.Instance);
        return writeToProp?.GetValue(loggerConfig) as LoggerSinkConfiguration;
    }

    private void ConfigureBaseLogging(LoggerConfiguration loggerConfig)
    {
        Debug.Assert(this.PathFinderService is not null, "PathFinderService must be initialized before creating the logger");

        IConfigurationRoot? configuration = null;
        try
        {
            Directory.CreateDirectory(this.PathFinderService.LocalAppData);

            configuration = new ConfigurationBuilder()
               .SetBasePath(this.PathFinderService.LocalAppData)
               .AddJsonFile("logging.json", optional: true, reloadOnChange: true)
               .AddJsonFile($"logging.{this.PathFinderService.Mode}.json", optional: true, reloadOnChange: true)
               .Build();
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch
        {
            // We cannot create the directory or load configuration; proceed with defaults
        }
#pragma warning restore CA1031 // Do not catch general exception types

        if (configuration is not null)
        {
            _ = loggerConfig.ReadFrom.Configuration(configuration);
            if (Debugger.IsAttached)
            {
                _ = loggerConfig
                    .WriteTo.Debug(
                        new ExpressionTemplate(
                            "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                            new CultureInfo("en-US")));
            }
        }
        else if (Debugger.IsAttached)
        {
            _ = loggerConfig
                .MinimumLevel.Debug()
                .Enrich.FromLogContext()
                .WriteTo.Debug(
                    new ExpressionTemplate(
                        "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                        new CultureInfo("en-US")));
        }
    }

    /// <summary>
    ///     Configures the <see cref="Microsoft.Extensions.Logging" /> abstraction layer as the
    ///     preferred logging API for the application.
    /// </summary>
    /// <remarks>
    ///     Must be called after <see cref="Configure" /> but before <see cref="Build" />.
    ///     <para>
    ///     This method:
    ///     <list type="number">
    ///         <item>Creates and registers a Serilog logger factory</item>
    ///         <item>Handles proper disposal of the factory after container ownership transfer</item>
    ///         <item>
    ///             Registers two logger resolution strategies:
    ///             <list type="bullet">
    ///                 <item>Default logger for components without implementation type,</item>
    ///                 <item>Type-specific loggers for components with implementation type.</item>
    ///             </list>
    ///         </item>
    ///     </list>
    ///     The registration ensures that:
    ///     <list type="bullet">
    ///         <item>Components get appropriate loggers based on their type,</item>
    ///         <item>Logger instances are properly scoped and disposed,</item>
    ///         <item>Integration between Serilog and Microsoft.Extensions.Logging is configured.</item>
    ///     </list>
    ///     </para>
    /// </remarks>
    private void ConfigureLoggingAbstraction()
    {
        this.EnsureConfiguredButNotBuilt();

        _ = this.builder.ConfigureContainer<DryIocServiceProvider>(provider =>
        {
            var container = provider.Container;
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
        });
    }

    /// <summary>
    ///     Creates and configures the global Serilog logger instance.
    /// </summary>
    /// <param name="container">The IoC container, which can be used to register the OutputLog delegating sink.</param>
    /// <remarks>
    ///     Configuration behavior:
    ///     <list type="number">
    ///         <item>
    ///             In DEBUG mode:
    ///             <list type="bullet">
    ///                 <item>Uses Debug minimum level,</item>
    ///                 <item>Writes to Debug output with formatted template,</item>
    ///                 <item>Format: "[Time Level (SourceContext)] Message\nException",</item>
    ///             </list>
    ///         </item>
    ///         <item>
    ///             With provided configuration:
    ///             <list type="bullet">
    ///                 <item>Reads settings from Serilog configuration section,</item>
    ///                 <item>Supports runtime configuration updates,</item>
    ///             </list>
    ///         </item>
    ///     </list>
    ///     The logger is configured differently based on:
    ///     <list type="bullet">
    ///         <item>Build configuration (DEBUG vs RELEASE),</item>
    ///         <item>Application mode (Development vs Real),</item>
    ///         <item>Presence of external configuration.</item>
    ///     </list>
    /// </remarks>
    private void CreateLogger(IContainer? container = null, BootstrapOptions? options = null)
    {
        Log.CloseAndFlush();

        // https://github.com/serilog/serilog-settings-configuration
        var loggerConfig = new LoggerConfiguration();

        this.ConfigureBaseLogging(loggerConfig);

        // At this stage the early logger is configured.
        // Continue only if we are at the second stage with a non-null final container
        if (container is not null)
        {
            ConfigureLoggingControls(loggerConfig, container, options);
        }

        Log.Logger = loggerConfig.CreateLogger();

        if (options?.UseLoggingAbstraction == true)
        {
            this.ConfigureLoggingAbstraction();
        }
    }

    /// <summary>
    ///     Validates the bootstrapper state and returns the host builder if valid.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    ///     Thrown if the host has already been built,or Configure() hasn't been called yet.
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
    ///     Registers the services created during the early bootstrapping phase in the final container.
    /// </summary>
    /// <param name="container">The final container, passed by the host builder.</param>
    private void ConfigureEarlyServices(IContainer container, BootstrapOptions options)
    {
        // This is the final container that will be used by the application.
        this.finalContainer = container;

        Debug.Assert(this.PathFinderService is not null, "did you forget to register the IPathFinder service?");

        container.RegisterInstance(this.FileSystemService);
        container.RegisterInstance<IPathFinder>(this.PathFinderService!);

        this.CreateLogger(container, options);

        // Early services registration finished — log it. The wrapper will prefer the final
        // ILogger when available, otherwise fall back to Serilog early logging.
        this.LogEarlyServicesConfigured();
    }

    /// <summary>
    ///     Creates a PathFinder instance using the specified configuration.
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

    /// <summary>
    ///     Adds configuration files to the IConfigurationBuilder.
    ///     This method ensures that configuration files are de-duplicated.
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
}
