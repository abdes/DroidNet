// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Storage;

/// <summary>
/// The exception that is thrown when an attempt is made to create or copy an item, but an item with the same name already exists
/// in the target location.
/// </summary>
/// <remarks>
/// This exception typically occurs during operations such as file or folder creation or copying, where uniqueness of names in the
/// destination location is required.
/// </remarks>
/// <seealso cref="StorageException" />
public class TargetExistsException : StorageException
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TargetExistsException" /> class, setting the Message property of the new
    /// instance to a system-supplied message that describes the error.
    /// </summary>
    public TargetExistsException()
        : base("an item with same name exists at the target location")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="TargetExistsException" /> class with a specified error message.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    public TargetExistsException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="TargetExistsException" /> class with a specified error message and a reference
    /// to the inner exception that caused this exception.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    /// <param name="innerException">
    /// The exception that is the cause of the current exception, or a <see langword="null" /> reference if no inner exception is
    /// specified.
    /// </param>
    public TargetExistsException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
