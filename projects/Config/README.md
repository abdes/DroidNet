# DroidNet.Config

The **DroidNet.Config** module simplifies application configuration management
using [DryIoc](https://github.com/dadhi/DryIoc) for dependency injection, the
[Microsoft.Extensions.Configuration](https://learn.microsoft.com/en-us/dotnet/api/microsoft.extensions.options?view=net-8.0-pp)
framework for configuration loading, and its recommended [Options
Pattern](https://learn.microsoft.com/en-us/dotnet/core/extensions/options).

It streamlines configuration file handling, options pattern setup, and early
service registration, ensuring a consistent and maintainable configuration
experience.

## Features

- **Environment-based Configuration**: Switch effortlessly between `development`
  and `real` modes, and keep the same interface to obtain a variety of system,
  user and application specific locations, such as the Documents folder, the
  application LocalData, or Program Files locations.
- **JSON Persistence**: Persist settings changes to JSON files in the local app
  data folder.
- **Precise Property Change Notifications**: The Options Pattern is great, but
  change notifications are not property specific. DroidNet.Config Supports
  property change notifications using `INotifyPropertyChanged` and has an
  extension pattern for intuitive and type safe access to properties in config
  sections.

## Getting Started

```shell
dotnet add package DroidNet.Config
```

## Typical Usage

Here is a typical example of how it can be used in the Main entryp point of an
application. In this example, The application has configuration settings in a
`LocalSettings.json` file, which contains a section named `ThemeSettings`, with
a configuration option with the key Theme and a value of `"dark"`.

{% code title="LocalSettings.json" %}

```json
{
  "ThemeSettings": {
    "Theme": "Dark"
  }
}
```

{% endcode %}

Configuration files are typically stored in the `LocalAppData` directory. To
isolate the application from the details of how to get those paths, the library
exposes an `IPathFinder` interface. `IPathFinder` depends on the `IFileSystem`
abstraction for testability. Both should be registered in the Host's container
for IoC dependency injection.

Because we want to use the PathFinder before the container is built, we need to
follow a precise course of action when setting up the Host. The `IFileSystem`
and the `IPathFinder` instances are initially created manually, used to
configure configuration files and then added to the IoC container service
collection.

```csharp
public static partial class Program
{
    private static IPathFinder? PathFinderService { get; set; }
    private static IFileSystem FileSystemService { get; } = new RealFileSystem();

    private static void Main(string[] args)
    {
        try
        {
            var builder = Host.CreateDefaultBuilder(args)

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()))

                // Add configuration files and configure Options, but first, initialize the IPathFinder instance, so it
                // can be used to resolve configuration file paths.
                .ConfigureAppConfiguration(
                    (_, config) =>
                    {
                        // Build a temporary config to get access to the command line arguments.
                        // NOTE: we expect the `--mode dev|real` optional argument.
                        var tempConfig = config.Build();
                        PathFinderService = CreatePathFinder(tempConfig);
                    })
                .ConfigureAppConfiguration(AddConfigurationFiles)
                .ConfigureServices(ConfigureOptionsPattern)
                .ConfigureServices(ConfigurePersistentStateDatabase)

                // Continue configuration using DryIoc container API. Configure early services, including Logging. Note
                // however, that before this point, Logger injection cannot be used.
                .ConfigureContainer<DryIocServiceProvider>(provider => ConfigureEarlyServices(provider.Container));

            // Configurae any other services...

            // Finally start the host. This will block until the application lifetime is terminated through CTRL+C,
            // closing the UI windows or programmatically.
            builder.Build().Run();
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Host terminated unexpectedly");
        }
    }
```

The example sets up a .NET generic host with dependency injection,
configuration, and logging. It initializes using
`Host.CreateDefaultBuilder(args)`. The default builder already provides the
command line arguments as configuration options. However, to be able to use
them, we need to build a temporary configuration and explicitly (not as an IoC
service) create the instance of IPathFinder so that it can be used to resolve
configuration file paths.

```csharp
    private static PathFinder CreatePathFinder(IConfiguration configuration)
    {
#if DEBUG
        var mode = configuration["mode"] ?? PathFinder.DevelopmentMode;
#else
        var mode = configuration["mode"] ?? PathFinder.RealMode;
#endif
        var assembly = Assembly.GetEntryAssembly() ?? throw new CouldNotIdentifyMainAssemblyException();
        var companyName = GetAssemblyAttribute<AssemblyCompanyAttribute>(assembly)?.Company;
        var applicationName = GetAssemblyAttribute<AssemblyProductAttribute>(assembly)?.Product;

        var finderConfig = CreateFinderConfig(mode, companyName, applicationName);
        return new PathFinder(FileSystemService, finderConfig);
    }

    private static PathFinderConfig CreateFinderConfig(string mode, string? companyName, string? applicationName)
        => new(
            mode,
            companyName ?? throw new ArgumentNullException(nameof(companyName)),
            applicationName ?? throw new ArgumentNullException(applicationName));
```

The `CreatePathFinder` will look for the configuration _"mode"_, already added
by the default host builder from the command line arguments, to decide if it
should resolve paths in _"dev"_ mode or in _"real"_ mode.

Then it prepares the path finder configuration data and initializes the
instance.

Application configuration files are added in the `AddConfigurationFiles` method.
Here is the example's implementation to add the LocalSettings.json file:

```csharp
    private static void AddConfigurationFiles(IConfigurationBuilder config)
    {
        Debug.Assert(PathFinderService is not null, "must setup the PathFinderService before adding config files");

        // NOTE: Some settings classes may share the same configuration file, and only use a section
        // in it. We should only add the file once.
        var configFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            // TODO: PathFinderService.GetConfigFilePath(AppearanceSettings.ConfigFileName),
            PathFinderService.GetConfigFilePath("LocalSettings.json"),

            // More files as needed, duplicates are ok.
        };

        foreach (var configFile in configFiles)
        {
            _ = config.AddJsonFile(configFile, optional: true, reloadOnChange: true);
        }
    }
```

And the Options pattern configuration is created in the
`ConfigureOptionsPattern` method.

```csharp
    private static void ConfigureOptionsPattern(HostBuilderContext context, IServiceCollection sc)
    {
        var config = context.Configuration;
        _ = sc.Configure<ThemeSettings>(config.GetSection(nameof(ThemeSettings)));
    }
```

{% hint style="info" %} Note how both methods use the manually created instance
of `IPathFinder` to resolve the paths to the configuration files. {% endhint %}

The manually created services are finally made available in the IoC container
with:

```csharp
private static void ConfigureEarlyServices(IContainer container)
{
    container.RegisterInstance(FileSystemService);
    container.RegisterInstance(PathFinderService);

    // ... Add any other early services

    // Setup the CommunityToolkit.Mvvm Ioc helper
    Ioc.Default.ConfigureServices(container);

    Debug.Assert(
        PathFinderService is not null,
        "did you forget to register the IPathFinder service?");
    Log.Information(
        $"Application `{PathFinderService.ApplicationName}` starting in `{PathFinderService.Mode}` mode");
}
```

This is complicated and tricky because of the host builder predetermined order
of building things, and to the delayed instantiation of the services. Once the
host is built, injection will works fine and the `IPathFinder`, as well as the
`IFileSystem`, will be available to other services.
