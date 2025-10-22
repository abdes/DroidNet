// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.Helpers;

[ExcludeFromCodeCoverage]
public class TestContextLogger(TestContext testContext, string categoryName) : ILogger
{
    public IDisposable? BeginScope<TState>(TState state)
        where TState : notnull
        => null;

    public bool IsEnabled(LogLevel logLevel) => true;

    public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
    {
        var message = formatter(state, exception);
        testContext.WriteLine($"[{logLevel}] [{categoryName}] {message}");
        if (exception != null)
        {
            testContext.WriteLine($"Exception: {exception}");
        }
    }
}
