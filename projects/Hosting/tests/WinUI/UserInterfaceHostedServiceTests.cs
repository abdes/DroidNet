// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Hosting.WinUI;
using Moq;

namespace DroidNet.Hosting.Tests.WinUI;

/// <summary>
///     Unit tests for <see cref="UserInterfaceHostedService" />.
/// </summary>
[TestClass]
[TestCategory("Lifecycle")]
[ExcludeFromCodeCoverage]
public class UserInterfaceHostedServiceTests
{
    /// <summary>
    ///     Verifies that, when started, the UI service will attempt to start the
    ///     UI thread.
    /// </summary>
    /// <param name="cancellation">
    ///     A cancellation token that is used to check if the `Start` request has
    ///     been cancelled somewhere else.
    /// </param>
    /// <returns>Asynchronous task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task StartingTheService_WillStartTheUserInterface_UnlessCancelled(bool cancellation)
    {
        var mockContext = new Mock<HostingContext>(true);
        var mockThread = new Mock<IUserInterfaceThread>();
        var sut = new UserInterfaceHostedService(mockContext.Object, mockThread.Object, loggerFactory: null);

        var cancellationToken = new CancellationToken(cancellation);
        await sut.StartAsync(cancellationToken).ConfigureAwait(false);
        mockThread.Verify(m => m.StartUserInterface(), cancellation ? Times.Never() : Times.Once());
    }

    /// <summary>
    ///     Verifies that, when stopped, the UI service will attempt to stop the
    ///     UI thread.
    /// </summary>
    /// <param name="cancellation">
    ///     A cancellation token that is used to check if the `Stop` request has
    ///     been cancelled somewhere else.
    /// </param>
    /// <returns>Asynchronous task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task StoppingTheService_WillStopTheUserInterface_UnlessCancelled(bool cancellation)
    {
        var mockContext = new Mock<HostingContext>(true);
        var mockThread = new Mock<IUserInterfaceThread>();
        var sut = new UserInterfaceHostedService(mockContext.Object, mockThread.Object, loggerFactory: null);

        await sut.StartAsync(CancellationToken.None).ConfigureAwait(false);

        // Force the state to be `running` as it is expected when a stop is
        // request.
        mockContext.Object.IsRunning = true;

        var cancellationToken = new CancellationToken(cancellation);
        await sut.StopAsync(cancellationToken).ConfigureAwait(false);
        mockThread.Verify(m => m.StopUserInterfaceAsync(), cancellation ? Times.Never() : Times.Once());
    }
}
