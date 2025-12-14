// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Base class for ViewModel tests that need a configured <see cref="ILoggerFactory"/>.
/// Derive your test classes from this to automatically create and dispose a LoggerFactory for each test.
/// </summary>
public abstract class ViewModelTestBase
{
    /// <summary>
    /// Gets the ILoggerFactory configured for tests. Available in derived classes.
    /// </summary>
    protected ILoggerFactory? LoggerFactoryInstance { get; private set; }

    [TestInitialize]
    public void TestInitializeLogger()
        => this.LoggerFactoryInstance = LoggerFactory.Create(builder =>
        {
            _ = builder.AddDebug();
            _ = builder.SetMinimumLevel(LogLevel.Trace);
        });

    [TestCleanup]
    public void TestCleanupLogger() => this.LoggerFactoryInstance?.Dispose();
}
