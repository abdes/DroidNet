// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.Helpers;

[ExcludeFromCodeCoverage]
public sealed class TestContextLoggerProvider(TestContext testContext) : ILoggerProvider
{
    public ILogger CreateLogger(string categoryName) => new TestContextLogger(testContext, categoryName);

    public void Dispose()
    {
        // Nothing to dispose
    }
}
