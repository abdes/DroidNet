// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.TestHelpers;
using DryIoc;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Logging;
using Serilog.Core;
using Serilog.Events;

namespace Oxygen.Editor.Data.Tests;

[ExcludeFromCodeCoverage]
public class DatabaseTests : TestSuiteWithAssertions
{
    private readonly SqliteConnection dbConnection = new("Data Source=:memory:");
    private bool disposed;

    public DatabaseTests()
    {
        LoggingLevelSwitch.MinimumLevel = LogEventLevel.Information;
        var loggerFactory = this.Container.Resolve<ILoggerFactory>();
        this.Container.RegisterInstance(
            new DbContextOptionsBuilder<PersistentState>()
                .UseLoggerFactory(loggerFactory)
                .EnableDetailedErrors()
                .EnableSensitiveDataLogging()
                .UseSqlite(this.dbConnection)
                .Options);

        // Register the EF DbContext as transient so each Resolve gets a fresh `PersistentState` instance.
        // Reasons:
        // - Isolation: a new DbContext per resolution avoids shared change-tracking, cached entities, and leaked state
        //   between tests or between resolves inside a test, making tests deterministic.
        // - Safety: DbContext is not thread-safe; short-lived instances reduce accidental concurrent use of the same
        //   instance.
        // - Cleanup: `allowDisposableTransient: true` lets the test container dispose transient `IDisposable`
        //   instances (the DbContext) when the container is disposed, preventing resource leaks without
        //   requiring explicit scope management in each test.
        // Note: If a test needs a single shared DbContext across multiple resolutions to represent a unit-of-work,
        // consider using `Reuse.Scoped` and explicitly creating/disposing a scope for that scenario.
        this.Container.Register<PersistentState>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));

        this.dbConnection.Open();

        // Enable WAL mode for better concurrency
        using (var walCommand = this.dbConnection.CreateCommand())
        {
            walCommand.CommandText = "PRAGMA journal_mode=WAL;";
            _ = walCommand.ExecuteNonQuery();
        }

        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();

        // For tests using an in-memory SQLite connection, use EnsureCreated()
        // to create the schema from the current model instead of applying
        // migrations. This avoids the "pending model changes" error when
        // the model and migrations are out of sync.
        var success = db.Database.EnsureCreated();
        _ = success.Should().BeTrue("Database schema should be created successfully for tests.");
    }

    protected IContainer Container { get; } = TestContainer.CreateChild();

    /// <summary>
    /// Disposes the resources used by the <see cref="DatabaseSchemaTests"/> class.
    /// </summary>
    /// <param name="disposing">A value indicating whether the method is called from Dispose.</param>
    protected override void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.dbConnection.Dispose();
            this.Container.Dispose();
        }

        this.disposed = true;
        base.Dispose(disposing);
    }
}
