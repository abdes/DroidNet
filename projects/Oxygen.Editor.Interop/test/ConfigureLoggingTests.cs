// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.EngineInterface;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(EngineRunner))]
public sealed class ConfigureLoggingTests : IDisposable
{
    [SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "It is disposed")]
    private readonly EngineRunner runner = new();

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public void ConfigureLogging_WithLogger_EmitsConfiguredMessage()
    {
        var config = new LoggingConfig { Verbosity = 0, IsColored = false, ModuleOverrides = null }; // INFO
        var logger = new TestLogger();

        var ok = this.runner.ConfigureLogging(config, logger);
        var found = logger.Messages.Exists(
            m => m.Contains("logging configured", StringComparison.OrdinalIgnoreCase));

        _ = ok.Should().BeTrue("ConfigureLogging returned false");
        _ = found.Should().BeTrue($"Expected a log containing 'logging configured' but got: {string.Join(" | ", logger.Messages)}");
    }

    [TestMethod]
    public void ConfigureLogging_WithDefaultConfig_Success()
    {
        var config = new LoggingConfig();
        var result = this.runner.ConfigureLogging(config);
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithNullLogVModules_Success()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = null };
        var result = this.runner.ConfigureLogging(config);
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithEmptyModuleOverrides_Success()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = string.Empty };
        var result = this.runner.ConfigureLogging(config);
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithValidVModules_Success()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = "MyModule=2,*=OFF" };
        var result = this.runner.ConfigureLogging(config);
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void ConfigureLogging_WithMalformedVModules_Fail()
    {
        var config = new LoggingConfig { Verbosity = 2, IsColored = false, ModuleOverrides = "foo,bar" };
        var result = this.runner.ConfigureLogging(config);
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    public void ConfigureLogging_WithOutOfRangeVerbosity_Fail()
    {
        foreach (var v in new[] { -10, -100, 10, 42 })
        {
            var config = new LoggingConfig { Verbosity = v, IsColored = false, ModuleOverrides = null };
            var result = this.runner.ConfigureLogging(config);
            _ = result.Should().BeFalse(string.Create(CultureInfo.InvariantCulture, $"ConfigureLogging should fail for out-of-range verbosity {v}"));
        }
    }

    private void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.runner.Dispose();
        }
    }

    // Helper logger used for tests. Must expose a `Log` method with 5 parameters
    // (LogLevel, EventId, object state, Exception exception, formatter) so the
    // EngineRunner reflection can discover it.
    private sealed class TestLogger : ILogger
    {
        public List<string> Messages { get; } = [];

        public IDisposable BeginScope<TState>(TState state)
            where TState : notnull
            => NullScope.Instance;

        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(
            LogLevel level,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            ArgumentNullException.ThrowIfNull(formatter);

            var text = formatter(state, exception);
            this.Messages.Add(text);
        }

        private sealed class NullScope : IDisposable
        {
            public static NullScope Instance { get; } = new NullScope();

            public void Dispose()
            {
            }
        }
    }
}
