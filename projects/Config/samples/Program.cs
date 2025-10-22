// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Bootstrap;
using DryIoc;
using Microsoft.Extensions.Logging;
using Serilog;
using Testably.Abstractions;

namespace DroidNet.Config.Example;

#pragma warning disable CA1303 // Do not pass literals as localized parameters

internal static class Program
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "catch any unhandled exceptions")]
    private static async Task Main(string[] args)
    {
        if (args.Length == 0 || string.IsNullOrWhiteSpace(args[0]))
        {
            Console.WriteLine("Usage: Dotnet run --project <project> <config-folder-path>");
            Console.WriteLine("Provide the path to the folder that contains appsettings.json and related files.");
            return;
        }

        var samplesPath = Path.GetFullPath(args[0]);
        if (!Directory.Exists(samplesPath))
        {
            Console.WriteLine($"Config folder not found: {samplesPath}");
            return;
        }

        // Create bootstrapper and configure logging via the same pattern used in other apps
        var bootstrap = new Bootstrap.Bootstrapper(args);

        try
        {
            await Initialize(bootstrap, samplesPath).ConfigureAwait(true);
            await Run(bootstrap).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Host terminated unexpectedly");
        }
        finally
        {
            await Log.CloseAndFlushAsync().ConfigureAwait(true);
            bootstrap.Dispose();
        }
    }

    private static async Task Initialize(Bootstrapper bootstrap, string samplesPath)
    {
        // Configure and register application services into the bootstrapper's container
        _ = bootstrap.Configure()
            .WithLoggingAbstraction()
            .WithAppServices(container =>
            {
                // Register Testably RealFileSystem as the System.IO.Abstractions IFileSystem expected by Config
                container.RegisterInstance<System.IO.Abstractions.IFileSystem>(new RealFileSystem());

                // Register Config module and our sources/services
                _ = container.WithConfig();
                _ = container.WithJsonConfigSource("base", Path.Combine(samplesPath, "settings.json"), watch: true);

                var pathFinder = container.Resolve<IPathFinder>();
                if (pathFinder is not null)
                {
                    _ = container.WithJsonConfigSource("dev", Path.Combine(samplesPath, $"settings.{pathFinder.Mode}.json"), watch: true);
                }

                _ = container.WithJsonConfigSource("user", Path.Combine(samplesPath, "settings.user.json"), watch: true);
                _ = container.WithSettings<IAppSettings, AppSettingsService>();
            });

        // Build the host so the final container is available
        var host = bootstrap.Build();

        // set working directory to samples path so relative file operations behave as expected
        Directory.SetCurrentDirectory(samplesPath);

        // Resolve a logger from the bootstrapper container and log startup info
        var loggerFactory = bootstrap.Container.Resolve<ILoggerFactory>();

        // Resolve the manager from the bootstrapper container and initialize
        var manager = bootstrap.Container.Resolve<SettingsManager>();
        await manager.InitializeAsync().ConfigureAwait(true);
    }

    private static async Task Run(Bootstrapper bootstrap)
    {
        var actions = new Dictionary<string, Func<string[], Task>>(StringComparer.OrdinalIgnoreCase)
        {
            ["show"] = ShowAction,
            ["toggle"] = ToggleAction,
            ["setname"] = SetNameAction,
            ["autosave"] = AutoSaveAction,
            ["autodelay"] = AutoSaveDelayAction,
            ["save"] = SaveAction,
            ["reload"] = ReloadAction,
        };

        Console.WriteLine("Settings loaded. Enter commands: show | toggle | setname <name> | autosave [on|off|toggle|status] | autodelay <seconds> | save | reload | exit");

        while (true)
        {
            Console.Write("> ");
            var line = Console.ReadLine();
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            var parts = line.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            var cmd = parts[0];

            if (string.Equals(cmd, "exit", StringComparison.OrdinalIgnoreCase))
            {
                break;
            }

            if (!actions.TryGetValue(cmd, out var action))
            {
                action = UnknownAction;
            }

            await action(parts).ConfigureAwait(true);
        }

        // Local methods for each command to keep logic separated and testable
        Task ShowAction(string[] p)
        {
            // Obtain the typed settings service
            var svc = bootstrap.Container.Resolve<ISettingsService<IAppSettings>>();

            Console.WriteLine($"ApplicationName: {svc.Settings.ApplicationName}");
            Console.WriteLine($"LoggingLevel: {svc.Settings.LoggingLevel}");
            Console.WriteLine($"EnableExperimental: {svc.Settings.EnableExperimental}");
            return Task.CompletedTask;
        }

        Task ToggleAction(string[] p)
        {
            // Obtain the typed settings service
            var svc = bootstrap.Container.Resolve<ISettingsService<IAppSettings>>();

            svc.Settings.EnableExperimental = !svc.Settings.EnableExperimental;
            Console.WriteLine($"Toggled EnableExperimental -> {svc.Settings.EnableExperimental}");
            return Task.CompletedTask;
        }

        Task SetNameAction(string[] p)
        {
            // Obtain the typed settings service
            var svc = bootstrap.Container.Resolve<ISettingsService<IAppSettings>>();

            if (p.Length == 2)
            {
                svc.Settings.ApplicationName = p[1];
                Console.WriteLine($"Set ApplicationName -> {svc.Settings.ApplicationName}");
            }
            else
            {
                Console.WriteLine("Usage: setname <name>");
            }

            return Task.CompletedTask;
        }

        Task AutoSaveAction(string[] p)
        {
            // Obtain the manager instance
            var manager = bootstrap.Container.Resolve<SettingsManager>();

            if (p.Length == 1)
            {
                Console.WriteLine($"AutoSave: {manager.AutoSave} (Delay: {manager.AutoSaveDelay.TotalSeconds} sec)");
                return Task.CompletedTask;
            }

            var arg = p[1].Trim();

            if (string.Equals(arg, "toggle", StringComparison.OrdinalIgnoreCase))
            {
                manager.AutoSave = !manager.AutoSave;
                Console.WriteLine($"AutoSave -> {manager.AutoSave}");
                return Task.CompletedTask;
            }

            if (string.Equals(arg, "on", StringComparison.OrdinalIgnoreCase) || string.Equals(arg, "true", StringComparison.OrdinalIgnoreCase))
            {
                manager.AutoSave = true;
                Console.WriteLine("AutoSave enabled.");
                return Task.CompletedTask;
            }

            if (string.Equals(arg, "off", StringComparison.OrdinalIgnoreCase) || string.Equals(arg, "false", StringComparison.OrdinalIgnoreCase))
            {
                manager.AutoSave = false;
                Console.WriteLine("AutoSave disabled.");
                return Task.CompletedTask;
            }

            if (string.Equals(arg, "status", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine($"AutoSave: {manager.AutoSave} (Delay: {manager.AutoSaveDelay.TotalSeconds} sec)");
                return Task.CompletedTask;
            }

            Console.WriteLine("Usage: autosave [on|off|toggle|status]");
            return Task.CompletedTask;
        }

        async Task SaveAction(string[] p)
        {
            // Obtain the typed settings service
            var svc = bootstrap.Container.Resolve<ISettingsService<IAppSettings>>();

            Console.WriteLine("Saving settings...");
            await svc.SaveAsync().ConfigureAwait(true);
            Console.WriteLine("Saved.");
        }

        async Task ReloadAction(string[] p)
        {
            // Obtain the typed settings service
            var manager = bootstrap.Container.Resolve<ISettingsManager>();

            Console.WriteLine("Reloading all sources...");
            await manager.ReloadAllAsync().ConfigureAwait(true);
            Console.WriteLine("Reloaded.");
        }

        Task AutoSaveDelayAction(string[] p)
        {
            var manager = bootstrap.Container.Resolve<SettingsManager>();

            if (p.Length == 2 && double.TryParse(p[1], NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.InvariantCulture, out var seconds))
            {
                if (seconds <= 0)
                {
                    Console.WriteLine("Delay must be positive.");
                    return Task.CompletedTask;
                }

                manager.AutoSaveDelay = TimeSpan.FromSeconds(seconds);
                Console.WriteLine($"AutoSaveDelay -> {manager.AutoSaveDelay.TotalSeconds} seconds");
                return Task.CompletedTask;
            }

            Console.WriteLine("Usage: autodelay <seconds>");
            return Task.CompletedTask;
        }

        Task UnknownAction(string[] p)
        {
            Console.WriteLine("Unknown command");
            return Task.CompletedTask;
        }
    }
}
