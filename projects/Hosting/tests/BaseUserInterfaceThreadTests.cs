// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting;

using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Moq.Protected;

/// <summary>
/// Uint tests for the <see cref="BaseUserInterfaceThread{T}" /> class.
/// </summary>
[TestClass]
public class BaseUserInterfaceThreadTests
{
    /// <summary>
    /// Tests the different lifecycle actions during the User Interface Thread
    /// startup.
    /// </summary>
    [TestMethod]
    [TestCategory("Lifecycle")]
    public void FromStartToFinish()
    {
        var mockContext = new Mock<BaseHostingContext>(true);
        var mockLifeTime = new Mock<IHostApplicationLifetime>();
        var mockLogger = new Mock<ILogger>();

        var mockThread = new Mock<BaseUserInterfaceThread<BaseHostingContext>>(
            mockLifeTime.Object,
            mockContext.Object,
            mockLogger.Object)
        {
            CallBase = true,
        };
        mockThread.Protected()
            .Setup("BeforeStart");
        mockThread.Protected()
            .Setup("DoStart");

        // Start he UI thread and wait until it completes before testing for assertions.
        var thread = mockThread.Object;
        thread.StartUserInterface();
        thread.AwaitUiThreadCompletion();

        mockThread.Protected()
            .Verify("BeforeStart", Times.Once());
        mockThread.Protected()
            .Verify("DoStart", Times.Once());
    }

    /// <summary>
    /// Tests that when the application lifetime and the UI thread lifetime are
    /// linked, completion of the UI thread leads to the application stopping.
    /// </summary>
    [TestMethod]
    [TestCategory("Lifecycle")]
    public void LinkedLifetimeCompletion()
    {
        var mockContext = new Mock<BaseHostingContext>(true);

        var mockLifeTime = new Mock<IHostApplicationLifetime>();
        var mockLogger = new Mock<ILogger>();

        var mockThread = new Mock<BaseUserInterfaceThread<BaseHostingContext>>(
            mockLifeTime.Object,
            mockContext.Object,
            mockLogger.Object)
        {
            CallBase = true,
        };

        var thread = mockThread.Object;

        // Start he UI thread and wait until it completes before testing for assertions.
        thread.StartUserInterface();
        thread.AwaitUiThreadCompletion();

        mockLifeTime.Verify(m => m.StopApplication());
    }

    /// <summary>
    /// Tests that when the application lifetime and the UI thread lifetime are
    /// not linked, completion of the UI thread does not result in the
    /// application stopping.
    /// </summary>
    [TestMethod]
    [TestCategory("Lifecycle")]
    public void IndependentLifetimeCompletion()
    {
        var mockContext = new Mock<BaseHostingContext>(false);

        var mockLifeTime = new Mock<IHostApplicationLifetime>();
        var mockLogger = new Mock<ILogger>();

        var mockThread = new Mock<BaseUserInterfaceThread<BaseHostingContext>>(
            mockLifeTime.Object,
            mockContext.Object,
            mockLogger.Object)
        {
            CallBase = true,
        };

        var thread = mockThread.Object;

        // Start he UI thread and wait until it completes before testing for assertions.
        thread.StartUserInterface();
        thread.AwaitUiThreadCompletion();

        mockLifeTime.Verify(m => m.StopApplication(), Times.Never);
    }
}
