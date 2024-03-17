// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Demo.Services;

using Microsoft.Extensions.Logging;

/// <summary>A simple test service for demo purpose.</summary>
internal sealed partial class TestService : ITestInterface, IDisposable
{
    private readonly string requester;
    private readonly ILogger logger;

    public TestService(ILogger logger, string requester)
    {
        this.logger = logger;
        this.LogCreation(requester);
        this.requester = requester;
    }

    public string Message => $"Message requested from {this.requester}";

    public void Dispose()
    {
        this.LogDisposal();
        GC.SuppressFinalize(this);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Service instance created by {Requester}")]
    private partial void LogCreation(string requester);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Service instance disposed of")]
    private partial void LogDisposal();
}
