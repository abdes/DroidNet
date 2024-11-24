// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.CommandLine;
using System.CommandLine.Parsing;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Config;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Design;
using Oxygen.Editor.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Data;

/// <summary>
/// A factory for creating instances of <see cref="PersistentState"/> at design time, used by Entity
/// Framework Core tools to create the DbContext when running commands such as migrations.
/// </summary>
/// <remarks>
/// Using a design-time factory allows bypassing the complexity of the IoC container and HostBuilder
/// setup in the main program. This approach is useful for configuring the DbContext differently for
/// design time, handling additional parameters, or avoiding DI setup.
/// <para>
/// The following command line arguments are supported:
/// </para>
/// <para>
/// <strong>--mode</strong>: Specifies the configuration mode for the <see cref="PathFinder"/>
/// (dev or real), used to obtain the path for the persistent store database. Default is dev.
/// </para>
/// <para>
/// <strong>--use-in-memory-db</strong>: Do not use a database file and use an in-memory SQLite
/// database. Particularly useful for testing and initial migration creation.
/// </para>
/// <para>
/// <strong>Note:</strong> Only one of the options should be used at a time. If both are specified,
/// an <see cref="ArgumentException"/> is thrown.
/// </para>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <para>
/// Using an in-memory SQLite database:
/// </para>
/// <code><![CDATA[
/// dotnet ef migrations add InitialCreate -- --use-in-memory-db
/// ]]></code>
/// <para>
/// Using a file-based SQLite database in development mode:
/// </para>
/// <code><![CDATA[
/// dotnet ef migrations add InitialCreate -- --mode=dev
/// ]]></code>
/// </example>
/// </remarks>
[SuppressMessage("ReSharper", "UnusedMember.Global", Justification = "Used by EF Core tools")]
public class DesignTimePersistentStateFactory : IDesignTimeDbContextFactory<PersistentState>
{
    /// <summary>
    /// Creates a new instance of <see cref="PersistentState"/> with the specified options.
    /// </summary>
    /// <param name="args">Arguments passed by the design-time tools. Supports --mode and c options.</param>
    /// <returns>A new instance of <see cref="PersistentState"/>.</returns>
    /// <exception cref="ArgumentException">Thrown when both `--mode and` `--use-in-memory-db` options are specified.</exception>
    public PersistentState CreateDbContext(string[] args)
    {
        var optionsBuilder = new DbContextOptionsBuilder<PersistentState>();

        ParseCommandLineArgs(args, out var useInMemoryDb, out var mode);

        switch (useInMemoryDb)
        {
            case true:
                _ = optionsBuilder.UseSqlite("Data Source=:memory:");
                break;
            default:
                {
                    var pathFinderConfig = new PathFinderConfig(
                        Mode: mode ?? PathFinder.DevelopmentMode,
                        CompanyName: Constants.Company,
                        ApplicationName: Constants.Application);

                    var pathFinder = new PathFinder(new RealFileSystem(), pathFinderConfig);
                    var databasePath = Path.Combine(pathFinder.LocalAppData, "PersistentState.db");

                    _ = optionsBuilder.UseSqlite($"Data Source={databasePath}");
                    break;
                }
        }

        return new PersistentState(optionsBuilder.Options);
    }

    private static void ParseCommandLineArgs(string[] args, out bool useInMemoryDb, out string? mode)
    {
        // Define command-line options
        var modeOption = new Option<string>(
            "--mode",
            description: "Specifies the mode for the PathFinder (dev or real).");

        var useInMemoryDbOption = new Option<bool>(
            "--use-in-memory-db",
            getDefaultValue: () => false,
            description: "Specifies whether to use an in-memory SQLite database.");

        // Create a root command with the options
        var rootCommand = new RootCommand
        {
            modeOption,
            useInMemoryDbOption,
        };

        // Parse the command-line arguments
        var parseResult = rootCommand.Parse(args);

        if (parseResult.HasOption(modeOption) && parseResult.HasOption(useInMemoryDbOption))
        {
            throw new ArgumentException("Only one of --mode or --use-in-memory-db should be specified.", paramName: nameof(args));
        }

        useInMemoryDb = parseResult.GetValueForOption(useInMemoryDbOption);
        mode = parseResult.GetValueForOption(modeOption);
    }
}
