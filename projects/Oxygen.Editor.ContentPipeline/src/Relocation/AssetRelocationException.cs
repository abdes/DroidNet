// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>A relocation, copy or delete was rejected before changing any file; the message tells the user why.</summary>
public sealed class AssetRelocationException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class.</summary>
    public AssetRelocationException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class.</summary>
    /// <param name="message">Why the operation was rejected.</param>
    public AssetRelocationException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class.</summary>
    /// <param name="message">Why the operation was rejected.</param>
    /// <param name="innerException">The underlying failure.</param>
    public AssetRelocationException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
