// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.TestHelpers;

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>
///     A lightweight <see cref="ILoggerProvider"/> implementation used in tests that records log
///     messages to an in-memory list.
/// </summary>
public sealed class TestLoggerProvider : ILoggerProvider
{
    // Backing collection for captured messages.
    private readonly IList<string> messages = [];

    /// <summary>
    ///     Gets the read-only collection of messages captured by loggers created from this
    ///     provider.
    /// </summary>
    public IReadOnlyList<string> Messages => this.messages.AsReadOnly();

    /// <summary>
    ///     Creates a new <see cref="ILogger"/> instance that appends messages to the provider's
    ///     internal message list.
    /// </summary>
    /// <param name="categoryName">
    ///     The category name for messages produced by the logger. This implementation ignores the
    ///     category and returns a test logger that writes to the shared message list.
    /// </param>
    /// <returns>An <see cref="ILogger"/> instance.</returns>
    public ILogger CreateLogger(string categoryName) => new TestLogger(this.messages);

    /// <summary>
    ///     Releases resources used by the provider. This implementation has no resources to
    ///     dispose.
    /// </summary>
    public void Dispose()
    {
    }
}

/// <summary>
///     An <see cref="ILogger"/> implementation for tests which appends formatted log messages into
///     a shared <see cref="IList{String}"/> provided by the <see cref="TestLoggerProvider"/>.
/// </summary>
/// <param name="messages">The backing list to which messages will be appended.</param>
public sealed class TestLogger(IList<string> messages) : ILogger
{
    /// <summary>
    ///     Gets a read-only view over the backing message list for this logger.
    /// </summary>
    public IList<string> Messages => messages.AsReadOnly();

    /// <inheritdoc />
    IDisposable ILogger.BeginScope<TState>(TState state) => NullScope.Instance;

    /// <summary>
    ///     Always returns <see langword="true"/> so that all levels are enabled for tests.
    /// </summary>
    /// <param name="logLevel">The log level to check.</param>
    /// <returns><see langword="true"/>.</returns>
    public bool IsEnabled(LogLevel logLevel) => true;

    /// <summary>
    ///     Formats and records the provided state and exception using the provided formatter. The
    ///     resulting string is appended to the backing message list.
    /// </summary>
    /// <typeparam name="TState">The type of the state object.</typeparam>
    /// <param name="logLevel">The log level.</param>
    /// <param name="eventId">The event id.</param>
    /// <param name="state">The log state.</param>
    /// <param name="exception">An optional exception associated with the log.</param>
    /// <param name="formatter">A formatter used to produce the log message text.</param>
    public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
    {
        var text = formatter != null ? formatter(state, exception) : (state?.ToString() ?? string.Empty);
        lock (messages)
        {
            messages.Add(text);
        }
    }
}

/// <summary>
///     A singleton no-op scope used by <see cref="TestLogger"/> when BeginScope is called.
/// </summary>
public sealed class NullScope() : IDisposable
{
    /// <summary>
    ///     The single shared instance of <see cref="NullScope"/>.
    /// </summary>
    public static readonly NullScope Instance = new();

    /// <summary>
    ///     No-op dispose.
    /// </summary>
    public void Dispose()
    {
    }
}
