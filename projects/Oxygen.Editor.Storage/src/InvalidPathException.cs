// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

using System;

/// <summary>
/// Exception that is thrown when a storage item is created with an invalid path string or when its current location or name are
/// modified using invalid values.
/// </summary>
/// <remarks>
/// A path is a string that provides the location of a document or folder. Depending on the underlying storage system, a path does
/// not necessarily point to a location on disk; for example, a path might map to a remote location or to an item inside a binary
/// archive. The exact format of a path is determined by the current storage provider. The current storage provider also
/// determines the set of characters used to separate the elements of a path, and the set of characters that cannot be used when
/// specifying paths.
/// <para>
/// A path can contain absolute or relative location information. Absolute paths fully specify a location: the file or directory
/// can be uniquely identified regardless of the current location. Relative paths specify a partial location: the current location
/// is used as the starting point when locating a file specified with a relative path.
/// </para>
/// <para>
/// The <see cref="IStorageItem.Name">name</see> of a storage item is usually the last segment of the path that describes the
/// <see cref="IStorageItem.Location">location</see> of that item and as such, the same restrictions on its validity than the path
/// string apply.
/// </para>
/// </remarks>
public class InvalidPathException : StorageException
{
    /// <summary>
    /// Initializes a new instance of the <see cref="InvalidPathException" /> class, setting the Message property of the new
    /// instance to a system-supplied message that describes the error.
    /// </summary>
    public InvalidPathException()
        : base("the path for a storage item or its name is invalid")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="InvalidPathException" /> class with a specified error message.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    public InvalidPathException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="InvalidPathException" /> class with a specified error message and a reference
    /// to the inner exception that caused this exception.
    /// </summary>
    /// <param name="message">
    /// The message that describes the error.
    /// </param>
    /// <param name="innerException">
    /// The exception that is the cause of the current exception, or a <see langword="null" /> reference if no inner exception is
    /// specified.
    /// </param>
    public InvalidPathException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
