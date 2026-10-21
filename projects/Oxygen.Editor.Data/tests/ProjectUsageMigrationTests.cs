// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Tests;

/// <summary>The migrations create the schema the model reads and writes.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class ProjectUsageMigrationTests
{
    /// <summary>A database the migrations created saves and reads back a project's explorer location.</summary>
    [TestMethod]
    public void MigratedDatabaseKeepsTheExplorerLocation()
    {
        using var connection = new SqliteConnection("Data Source=:memory:");
        connection.Open();
        var options = new DbContextOptionsBuilder<PersistentState>().UseSqlite(connection).Options;
        using (var writer = new PersistentState(options))
        {
            writer.Database.Migrate();
            _ = writer.ProjectUsageRecords.Add(new ProjectUsage
            {
                Name = "Saved",
                Location = "C:/Projects/Saved",
                ContentExplorerState = "/(left:folders//right:assets/list)",
            });
            _ = writer.SaveChanges();
        }

        using var reader = new PersistentState(options);

        _ = reader.ProjectUsageRecords.Single().ContentExplorerState.Should().Be("/(left:folders//right:assets/list)");
    }
}
