// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.WinUI;

using System.Diagnostics.CodeAnalysis;
using System.Threading.Tasks;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>
/// Unit tests for <see cref="UserInterfaceHostedService" />.
/// </summary>
[TestClass]
[TestCategory("Lifecycle")]
[ExcludeFromCodeCoverage]
public class UserInterfaceHostedServiceTests
{
    /// <summary>
    /// Verifies that, when started, the UI service will attempt to start the
    /// UI thread.
    /// </summary>
    /// <param name="cancellation">
    /// A cancellation token that is used to check if the `Start` request has
    /// been cancelled somewhere else.
    /// </param>
    /// <returns>Asynchronous task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task StartingTheServiceWillStartTheUserInterfaceUnlessCancelled(bool cancellation)
    {
        var mockContext = new Mock<HostingContext>(true);
        var mockThread = new Mock<IUserInterfaceThread>();
        var sut = new UserInterfaceHostedService(loggerFactory: null, mockThread.Object, mockContext.Object);

        var cancellationToken = new CancellationToken(cancellation);
        await sut.StartAsync(cancellationToken);
        mockThread.Verify(m => m.StartUserInterface(), cancellation ? Times.Never() : Times.Once());
    }

    /// <summary>
    /// Verifies that, when stopped, the UI service will attempt to stop the
    /// UI thread.
    /// </summary>
    /// <param name="cancellation">
    /// A cancellation token that is used to check if the `Stop` request has
    /// been cancelled somewhere else.
    /// </param>
    /// <returns>Asynchronous task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task StoppingTheServiceWillStopTheUserInterfaceUnlessCancelled(bool cancellation)
    {
        var mockContext = new Mock<HostingContext>(true);
        var mockThread = new Mock<IUserInterfaceThread>();
        var sut = new UserInterfaceHostedService(loggerFactory: null, mockThread.Object, mockContext.Object);

        await sut.StartAsync(CancellationToken.None);

        // Force the state to be `running` as it is expected when a stop is
        // request.
        mockContext.Object.IsRunning = true;

        var cancellationToken = new CancellationToken(cancellation);
        await sut.StopAsync(cancellationToken);
        mockThread.Verify(m => m.StopUserInterfaceAsync(), cancellation ? Times.Never() : Times.Once());
    }
}
