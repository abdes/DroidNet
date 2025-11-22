// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.EngineInterface;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[SuppressMessage(
    "Design",
    "CA1001:Types that own disposable fields should be disposable",
    Justification = "MSTest lifecycle takes care of that")]
[TestCategory(nameof(EngineRunner))]
public sealed class ConfigureLoggingTests
{
    private EngineRunner runner = null!; // Initialized in TestInitialize

    [TestInitialize]
    public void Setup() => this.runner = new EngineRunner();

    [TestCleanup]
    public void Teardown()
    {
        this.runner.Dispose();
        this.runner = null!;
    }

    [TestMethod]
    public void ConfigureLogging_WithLogger_EmitsConfiguredMessage()
    {
        var config = new LoggingConfig { Verbosity = 0, IsColored = false, ModuleOverrides = null }; // INFO
        var logger = new TestLogger();

        var ok = this.runner.ConfigureLogging(config, logger);

        // Wait up to 1500ms for a log to arrive, polling to avoid flaky sleeps.
        var sw = Stopwatch.StartNew();
        var found = false;
        while (sw.ElapsedMilliseconds < 1500)
        {
            if (logger.Messages.Exists(m => m.Contains("logging configured", StringComparison.OrdinalIgnoreCase)))
            {
                found = true;
            }

            if (found)
            {
                break;
            }

            Thread.Sleep(10);
        }

        _ = ok.Should().BeTrue("ConfigureLogging returned false");
        _ = found.Should().BeTrue($"Expected a log containing 'logging configured' but got: {string.Join(" | ", logger.Messages)}");
    }

    [TestMethod]
    public void ConfigureLogging_WithDefaultConfig_Success()
    {
        var config = new LoggingConfig();
        var result = this.runner.ConfigureLogging(config);
        result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithNullLogVModules_Success()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = null };
        var result = this.runner.ConfigureLogging(config);
        result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithEmptyModuleOverrides_Success()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = string.Empty };
        var result = this.runner.ConfigureLogging(config);
        result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithValidVModules_Success()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = "MyModule=2,*=OFF" };
        var result = this.runner.ConfigureLogging(config);
        result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithMalformedVModules_Fail()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = "foo,bar" };
        var result = this.runner.ConfigureLogging(config);
        result.Should().BeFalse();
    }

    [TestMethod]
    public void ConfigureLogging_WithOutOfRangeVerbosity_Fail()
    {
        foreach (var v in new[] { -10, -100, 10, 42 })
        {
            var config = new LoggingConfig { Verbosity = v, IsColored = false, ModuleOverrides = null };
            var result = this.runner.ConfigureLogging(config);
            result.Should().BeFalse($"ConfigureLogging should fail for out-of-range verbosity {v}");
        }
    }

    // Helper logger used for tests. Must expose a `Log` method with 5 parameters
    // (LogLevel, EventId, object state, Exception exception, formatter) so the
    // EngineRunner reflection can discover it.
    private sealed class TestLogger
    {
        public List<string> Messages { get; } = new List<string>();

        [SuppressMessage("Design", "IDE0060: Remove unused parameter", Justification = "Used by reflection")]
        public void Log(
            LogLevel level,
            EventId eventId,
            object state,
            Exception? exception,
            Func<object, Exception?, string> formatter)
        {
            var text = formatter(state, exception);
            this.Messages.Add(text);
        }
    }
}
