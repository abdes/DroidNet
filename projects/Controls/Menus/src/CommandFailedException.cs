// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents errors that occur when a control's attached command fails to execute.
/// </summary>
[Serializable]
public sealed class CommandFailedException : Exception
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="CommandFailedException"/> class.
    /// </summary>
    public CommandFailedException()
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="CommandFailedException"/> class with a specified error message.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    public CommandFailedException(string message)
        : base(message)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="CommandFailedException"/> class with a specified error message and a
    ///     reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">The error message that explains the reason for the exception.</param>
    /// <param name="innerException">The exception that is the cause of the current exception.</param>
    public CommandFailedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
