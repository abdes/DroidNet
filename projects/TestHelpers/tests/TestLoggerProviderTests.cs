// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using FluentAssertions;
using Microsoft.Extensions.Logging;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class TestLoggerProviderTests : IDisposable
{
    private readonly TestLoggerProvider provider = new();
    private bool disposed;

    [TestInitialize]
    public void ClearProviderMessages()
    {
        // The provider exposes a read-only view; clear the backing list via reflection so
        // each test starts with a clean state.
        var field = typeof(TestLoggerProvider).GetField("messages", BindingFlags.NonPublic | BindingFlags.Instance);
        if (field?.GetValue(this.provider) is IList<string> list)
        {
            list.Clear();
        }
    }

    [TestMethod]
    public void Log_ShouldAppendFormattedMessage_ToProviderMessages()
    {
        // Arrange
        var logger = this.provider.CreateLogger("test");

        // Act
        logger.Log(LogLevel.Information, new EventId(1), "hello world", exception: null, (state, ex) => state);

        // Assert
        _ = this.provider.Messages.Should().ContainSingle().Which.Should().Be("hello world");
    }

    [TestMethod]
    public void MultipleLoggers_ShouldShareBackingMessageList()
    {
        // Arrange
        var logger1 = this.provider.CreateLogger("a");
        var logger2 = this.provider.CreateLogger("b");

        // Act
        logger1.Log(LogLevel.Information, new EventId(1), "one", exception: null, (s, e) => s);
        logger2.Log(LogLevel.Warning, new EventId(2), "two", exception: null, (s, e) => s);

        // Assert
        _ = this.provider.Messages.Should().HaveCount(2);
        _ = this.provider.Messages.Should().ContainInOrder(["one", "two"]);
    }

    [TestMethod]
    public void BeginScope_ShouldReturnSharedNullScopeInstance()
    {
        // Arrange
        var logger = this.provider.CreateLogger("scope");

        // Act
        var scope = ((ILogger)logger).BeginScope("anything");

        // Assert
        _ = scope.Should().BeSameAs(NullScope.Instance);

        // Cleanup
        scope.Dispose();
    }

    [TestMethod]
    public void IsEnabled_ShouldAlwaysReturnTrue()
    {
        // Arrange
        var logger = this.provider.CreateLogger("enabled");

        // Act & Assert
        _ = logger.IsEnabled(LogLevel.Trace).Should().BeTrue();
        _ = logger.IsEnabled(LogLevel.Critical).Should().BeTrue();
    }

    [TestMethod]
    public void Log_WithNullFormatter_ShouldUseStateToString()
    {
        // Arrange
        var logger = this.provider.CreateLogger("fmt");
        var state = new CustomState("state-to-string");

        // Act
        // pass null as formatter - code path should fall back to State.ToString()
        logger.Log<object>(LogLevel.Information, new EventId(0), state, exception: null, null!);

        // Assert
        _ = this.provider.Messages.Should().ContainSingle().Which.Should().Be("state-to-string");
    }

    /// <summary>
    /// Performs application-defined tasks associated with freeing, releasing, or resetting unmanaged resources.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Protected dispose pattern implementation.
    /// </summary>
    /// <param name="disposing">True when called from Dispose(); false when called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // Dispose managed resources
            this.provider?.Dispose();
        }

        // No unmanaged resources to free.
        this.disposed = true;
    }

    private sealed class CustomState(string value)
    {
        private readonly string value = value;

        public override string ToString() => this.value;
    }
}
