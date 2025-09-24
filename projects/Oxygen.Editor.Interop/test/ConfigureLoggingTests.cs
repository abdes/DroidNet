// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.EngineInterface;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(EngineRunner))]
public sealed class ConfigureLoggingTests
{
    [TestMethod]
    public void ConfigureLogging_WithDefaultConfig_Success()
    {
        // Arrange
        var config = new LoggingConfig();

        // Act
        var result = EngineRunner.ConfigureLogging(config);

        // Assert
        Assert.IsTrue(result);
    }

    [TestMethod]
    public void ConfigureLogging_WithNullLogVModules_Success()
    {
        // Arrange: ModuleOverrides is explicitly null
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = null };

        // Act
        var result = EngineRunner.ConfigureLogging(config);

        // Assert
        Assert.IsTrue(result);
    }

    [TestMethod]
    public void ConfigureLogging_WithEmptyModuleOverrides_Success()
    {
        // Arrange: ModuleOverrides is an empty string
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = string.Empty };

        // Act
        var result = EngineRunner.ConfigureLogging(config);

        // Assert
        Assert.IsTrue(result);
    }

    [TestMethod]
    public void ConfigureLogging_WithValidVModules_Success()
    {
        // Arrange: valid comma-separated overrides; specific module then wildcard OFF
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = "MyModule=2,*=OFF" };

        // Act
        var result = EngineRunner.ConfigureLogging(config);

        // Assert
        Assert.IsTrue(result);
    }

    [TestMethod]
    public void ConfigureLogging_WithMalformedVModules_Fail()
    {
        // Arrange: malformed module overrides
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = "foo,bar" };

        // Act
        var result = EngineRunner.ConfigureLogging(config);

        // Assert
        Assert.IsFalse(result);
    }

    [TestMethod]
    public void ConfigureLogging_WithOutOfRangeVerbosity_Fail()
    {
        // Arrange
        foreach (var v in new[] { -10, -100, 10, 42 })
        {
            var config = new LoggingConfig { Verbosity = v, IsColored = false, ModuleOverrides = null };

            // Act
            var result = EngineRunner.ConfigureLogging(config);

            // Assert
            Assert.IsFalse(result, $"ConfigureLogging should fail for out-of-range verbosity {v}");
        }
    }
}
