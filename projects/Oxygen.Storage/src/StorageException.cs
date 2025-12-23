// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Storage;

/// <summary>
/// The exception that is thrown when a storage operation fails.
/// </summary>
/// <remarks>
/// If this exception is thrown as a direct result of a previous exception, it should include a reference to the
/// previous exception in the <see cref="Exception.InnerException">InnerException</see> property.
/// The <see cref="Exception.InnerException">InnerException</see> property returns the same value that is passed into
/// the constructor, or null if no value was supplied.
/// </remarks>
public class StorageException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="StorageException" /> class, setting the Message property of the new
    /// instance to a system-supplied message that describes the error.
    /// </summary>
    public StorageException()
        : base("a storage related operation failed")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="StorageException" /> class with a specified error message.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    public StorageException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="StorageException" /> class with a specified error message and a
    /// reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    /// <param name="innerException">
    /// The exception that is the cause of the current exception, or a <see langword="null" /> reference if no inner exception is
    /// specified.
    /// </param>
    public StorageException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
