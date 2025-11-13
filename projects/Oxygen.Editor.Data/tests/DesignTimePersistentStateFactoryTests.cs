// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.EntityFrameworkCore;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[TestCategory(nameof(DesignTimePersistentStateFactory))]
[ExcludeFromCodeCoverage]
public class DesignTimePersistentStateFactoryTests
{
    [TestMethod]
    public void CreateDbContext_WithUseInMemoryDbSpecified_UsesInMemoryDatabase()
    {
        // Arrange
        var args = new[] { "--use-in-memory-db" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        var context = factory.CreateDbContext(args);

        // Assert
        _ = context.Should().NotBeNull();
        var options = context.Database.GetDbConnection().ConnectionString;
        _ = options.Should().Contain("Data Source=:memory:");
    }

    [TestMethod]
    public void CreateDbContext_WithBothModeAndUseInMemoryDbSpecified_ThrowsArgumentException()
    {
        // Arrange
        var args = new[] { "--mode=dev", "--use-in-memory-db" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        Action act = () => factory.CreateDbContext(args);

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("*Only one of --use-in-memory-db or another DB selection option should be specified*")
            .And.ParamName.Should().Be("args");
    }

    [TestMethod]
    public void CreateDbContext_WithBothDbPathAndUseInMemoryDbSpecified_ThrowsArgumentException()
    {
        // Arrange
        var args = new[] { "--db-path=C:\\test.db", "--use-in-memory-db" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        Action act = () => factory.CreateDbContext(args);

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("*Only one of --use-in-memory-db or another DB selection option should be specified*")
            .And.ParamName.Should().Be("args");
    }

    [TestMethod]
    public void CreateDbContext_WithAllThreeOptionsSpecified_ThrowsArgumentException()
    {
        // Arrange
        var args = new[] { "--mode=dev", "--db-path=C:\\test.db", "--use-in-memory-db" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        Action act = () => factory.CreateDbContext(args);

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("*Only one of --use-in-memory-db or another DB selection option should be specified*")
            .And.ParamName.Should().Be("args");
    }

    [TestMethod]
    public void CreateDbContext_WithModeSpecified_UsesPathFinderToLocateDatabase()
    {
        // Arrange
        var args = new[] { "--mode=dev" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        var context = factory.CreateDbContext(args);

        // Assert
        _ = context.Should().NotBeNull();
        var connectionString = context.Database.GetDbConnection().ConnectionString;
        _ = connectionString.Should().NotBeNullOrEmpty();
        _ = connectionString.Should().Contain("Data Source=");
        _ = connectionString.Should().Contain("PersistentState.db");
    }

    [TestMethod]
    public void CreateDbContext_WithDbPathSpecified_UsesSpecifiedPath()
    {
        // Arrange
        var testPath = "C:\\CustomPath\\MyDatabase.db";
        var args = new[] { $"--db-path={testPath}" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        var context = factory.CreateDbContext(args);

        // Assert
        _ = context.Should().NotBeNull();
        var connectionString = context.Database.GetDbConnection().ConnectionString;
        _ = connectionString.Should().Contain($"Data Source={testPath}");
    }

    [TestMethod]
    public void CreateDbContext_WithNoArgsSpecified_UsesDefaultDevMode()
    {
        // Arrange
        var args = Array.Empty<string>();
        var factory = new DesignTimePersistentStateFactory();

        // Act
        var context = factory.CreateDbContext(args);

        // Assert
        _ = context.Should().NotBeNull();
        var connectionString = context.Database.GetDbConnection().ConnectionString;
        _ = connectionString.Should().NotBeNullOrEmpty();
        _ = connectionString.Should().Contain("Data Source=");
        _ = connectionString.Should().Contain("PersistentState.db");
    }

    [TestMethod]
    public void CreateDbContext_WithModeAndDbPathButNotInMemory_DoesNotThrow()
    {
        // Arrange - mode and db-path can coexist, db-path takes precedence
        var testPath = "C:\\test.db";
        var args = new[] { "--mode=real", $"--db-path={testPath}" };
        var factory = new DesignTimePersistentStateFactory();

        // Act
        var context = factory.CreateDbContext(args);

        // Assert
        _ = context.Should().NotBeNull();
        var connectionString = context.Database.GetDbConnection().ConnectionString;
        _ = connectionString.Should().Contain($"Data Source={testPath}");
    }
}
