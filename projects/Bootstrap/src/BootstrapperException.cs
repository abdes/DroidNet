// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Bootstrap;

/// <summary>
/// Represents errors that occur during the bootstrapping process.
/// </summary>
public sealed class BootstrapperException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="BootstrapperException"/> class.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    public BootstrapperException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="BootstrapperException"/> class.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    /// <param name="innerException">The exception that is the cause of the current exception, or a null reference if no inner exception is specified.</param>
    public BootstrapperException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="BootstrapperException"/> class with a default error message.
    /// </summary>
    public BootstrapperException()
        : base("could not identify the main assembly")
    {
    }
}
