// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
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
            .WithMessage("*Only one of --mode or --use-in-memory-db should be specified*");
    }
}
