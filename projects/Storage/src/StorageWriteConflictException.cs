// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Storage;

/// <summary>A destination changed since its baseline was read or is owned by another writer.</summary>
public sealed class StorageWriteConflictException : IOException
{
    /// <summary>Initializes a new instance of the <see cref="StorageWriteConflictException"/> class.</summary>
    public StorageWriteConflictException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="StorageWriteConflictException"/> class.</summary>
    /// <param name="message">The conflict details.</param>
    public StorageWriteConflictException(string? message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="StorageWriteConflictException"/> class.</summary>
    /// <param name="message">The conflict details.</param>
    /// <param name="innerException">The underlying failure.</param>
    public StorageWriteConflictException(string? message, Exception? innerException)
        : base(message, innerException)
    {
    }
}
