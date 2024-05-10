// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple;

/// <summary>
/// Exception thrown when an attempt is made to resolve a View with a <see langword="null" /> type, or with a type that
/// has not been registered with the Dependency Injector.
/// </summary>
public class MissingViewException : Exception
{
    private const string DefaultMessage = "cannot resolve the View for a router outlet";

    private readonly Lazy<string> extendedMessage;

    public MissingViewException()
        : this(DefaultMessage)
    {
    }

    public MissingViewException(string? message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public MissingViewException(string? message, Exception? innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    /// <summary>
    /// Gets the View type, which resolution attempt resulted in this exception being thrown.
    /// </summary>
    public required Type ViewModelType { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (ViewModelType={this.ViewModelType}";
}
