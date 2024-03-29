// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Internal exception used to report a failure in creating an instance of a factory-managed type, such as dockables and docks.
/// </summary>
public sealed class ObjectCreationException : Exception
{
    private const string DefaultMessage = "could not create an instance of an object";

    private readonly Lazy<string> extendedMessage;

    public ObjectCreationException()
        : this(DefaultMessage)
    {
    }

    public ObjectCreationException(string? message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public ObjectCreationException(string? message, Exception? innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    /// <summary>Gets the type of the object which creation failed.</summary>
    public required Type ObjectType { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (ObjectType={this.ObjectType}";
}
