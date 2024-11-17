// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Represents an exception that is thrown when the creation of an instance of a factory-managed type, such as dockables and docks, fails.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="ObjectCreationException"/> is used internally to signal that an error occurred during the instantiation of an object
/// managed by the factory. This could happen due to various reasons such as invalid constructor arguments, missing dependencies, or
/// other runtime issues.
/// </para>
/// <para>
/// This exception provides additional context by including the type of the object that failed to be created.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To handle an <see cref="ObjectCreationException"/>, you can use the following code:
/// </para>
/// <code><![CDATA[
/// try
/// {
///     var dock = Dock.Factory.CreateDock(typeof(CustomDock), invalidArgs);
/// }
/// catch (ObjectCreationException ex)
/// {
///     Console.WriteLine($"Failed to create dock: {ex.Message}");
/// }
/// ]]></code>
/// </example>
public sealed class ObjectCreationException : Exception
{
    private const string DefaultMessage = "could not create an instance of an object";

    private readonly Lazy<string> extendedMessage;

    /// <summary>
    /// Initializes a new instance of the <see cref="ObjectCreationException"/> class with a default error message.
    /// </summary>
    public ObjectCreationException()
        : this(DefaultMessage)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ObjectCreationException"/> class with a specified error message.
    /// </summary>
    /// <param name="message">The error message that explains the reason for the exception.</param>
    public ObjectCreationException(string? message)
        : base(message)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ObjectCreationException"/> class with a specified error message and a reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">The error message that explains the reason for the exception.</param>
    /// <param name="innerException">The exception that is the cause of the current exception, or a <see langword="null"/> reference if no inner exception is specified.</param>
    public ObjectCreationException(string? message, Exception? innerException)
        : base(message, innerException)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Gets the type of the object which creation failed.
    /// </summary>
    public required Type ObjectType { get; init; }

    /// <inheritdoc/>
    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (ObjectType={this.ObjectType})";
}
