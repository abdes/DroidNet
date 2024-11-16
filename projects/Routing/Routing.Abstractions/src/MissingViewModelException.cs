// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Exception thrown when an attempt is made to resolve a ViewModel with a <see langword="null" /> type, or with a type that
/// has not been registered with the Dependency Injector.
/// </summary>
public class MissingViewModelException : Exception
{
    private const string DefaultMessage = "cannot resolve the Viewmodel for a router outlet";

    private readonly Lazy<string> extendedMessage;

    public MissingViewModelException()
        : this(DefaultMessage)
    {
    }

    public MissingViewModelException(string? message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public MissingViewModelException(string? message, Exception? innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    /// <summary>
    /// Gets the ViewModel type, which resolution attempt resulted in this exception being thrown.
    /// </summary>
    public Type? ViewModelType { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (ViewModelType={this.ViewModelType}";
}
