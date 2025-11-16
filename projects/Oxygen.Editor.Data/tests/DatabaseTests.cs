// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
using DryIoc;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Data.Tests;

[ExcludeFromCodeCoverage]
public partial class DatabaseTests : TestSuiteWithAssertions
{
    private readonly SqliteConnection dbConnection = new("Data Source=:memory:");
    private bool disposed;

    protected DatabaseTests()
    {
        var loggerFactory = this.Container.Resolve<ILoggerFactory>();
        this.Container.RegisterInstance(
            new DbContextOptionsBuilder<PersistentState>()
                .UseLoggerFactory(loggerFactory)
                .EnableDetailedErrors()
                .EnableSensitiveDataLogging()
                .UseSqlite(this.dbConnection)
                .Options);
        this.Container.Register<PersistentState>(Reuse.Scoped);

        this.dbConnection.Open();

        // Enable WAL mode for better concurrency
        using (var walCommand = this.dbConnection.CreateCommand())
        {
            walCommand.CommandText = "PRAGMA journal_mode=WAL;";
            _ = walCommand.ExecuteNonQuery();
        }

        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();
        db.Database.Migrate();
    }

    protected IContainer Container { get; } = CommonTestEnv.TestContainer.CreateChild();

    /// <summary>
    /// Disposes the resources used by the <see cref="DatabaseSchemaTests"/> class.
    /// </summary>
    public new void Dispose() => this.Dispose(disposing: true);

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
