// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Thrown when a <see cref="IRoute">route</see> configuration does not satisfy the requirements.
/// </summary>
public class RoutesConfigurationException : Exception
{
    private const string DefaultMessage = "route configuration is not valid";

    private readonly Lazy<string> extendedMessage;

    public RoutesConfigurationException()
        : this(DefaultMessage)
    {
    }

    public RoutesConfigurationException(string? message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public RoutesConfigurationException(string? message, Exception? innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    /// <summary>Gets the affected Route object that failed validation.</summary>
    public required IRoute FailedRoute { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (FailedRoute={this.FailedRoute}";
}
