// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Storage;

/// <summary>
/// The exception that is thrown when a storage operation on a storage item is attempted but the underlying physical storage item
/// does not exist.
/// </summary>
/// <remarks>
/// This exception typically occurs during document or folder operations when such file or folder does not actually exist.
/// </remarks>
/// <seealso cref="StorageException" />
public class ItemNotFoundException : StorageException
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ItemNotFoundException" /> class, setting the Message property of the new
    /// instance to a system-supplied message that describes the error.
    /// </summary>
    public ItemNotFoundException()
        : base("the underlying storage item does not exist")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ItemNotFoundException" /> class with a specified error message.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    public ItemNotFoundException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ItemNotFoundException" /> class with a specified error message and a reference
    /// to the inner exception that caused this exception.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    /// <param name="innerException">
    /// The exception that is the cause of the current exception, or a <see langword="null" /> reference if no inner exception is
    /// specified.
    /// </param>
    public ItemNotFoundException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
