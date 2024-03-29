// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger;

using DroidNet.Mvvm;

/// <summary>
/// Thrown when a View resolved for a certain ViewModel has a type that does not satisfy the requirements, such as not
/// implementing the <see cref="IViewFor{T}" /> or does not derive from a type that can be set as Content for an outlet.
/// </summary>
public class ViewTypeException : Exception
{
    private const string DefaultMessage = "view type not suitable to be used as Content type";

    private readonly Lazy<string> extendedMessage;

    public ViewTypeException()
        : this(DefaultMessage)
    {
    }

    public ViewTypeException(string? message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public ViewTypeException(string? message, Exception? innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public required Type ViewType { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (ViewType={this.ViewType}";
}
