// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Hosting.Demo.Services;

/// <summary>
/// A simple test service for demo purposes.
/// </summary>
[SuppressMessage("Microsoft.Performance", "CA1812:AvoidUninstantiatedInternalClasses", Justification = "This class is instantiated via Dependency Injection.")]
internal sealed partial class TestService : ITestInterface, IDisposable
{
    private readonly string requester;
    private readonly ILogger logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="TestService"/> class.
    /// </summary>
    /// <param name="logger">The logger instance to be used by this class.</param>
    /// <param name="requester">The name of the requester creating this service instance.</param>
    public TestService(ILogger logger, string requester)
    {
        this.logger = logger;
        this.LogCreation(requester);
        this.requester = requester;
    }

    /// <inheritdoc/>
    public string Message => $"Message requested from {this.requester}";

    /// <inheritdoc/>
    public void Dispose() => this.LogDisposal();

    /// <summary>
    /// Logs the creation of the service instance.
    /// </summary>
    /// <param name="requester">The name of the requester creating this service instance.</param>
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Service instance created by {Requester}")]
    private partial void LogCreation(string requester);

    /// <summary>
    /// Logs the disposal of the service instance.
    /// </summary>
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Service instance disposed of")]
    private partial void LogDisposal();
}
