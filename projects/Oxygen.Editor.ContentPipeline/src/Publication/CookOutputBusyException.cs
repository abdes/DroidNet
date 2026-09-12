// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Published output is owned by another reader or writer.</summary>
public sealed class CookOutputBusyException : IOException
{
    /// <summary>Initializes a new instance of the <see cref="CookOutputBusyException"/> class.</summary>
    public CookOutputBusyException()
        : base("Cooked content is in use.")
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookOutputBusyException"/> class.</summary>
    /// <param name="message">The action needed before publication can proceed.</param>
    public CookOutputBusyException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookOutputBusyException"/> class.</summary>
    /// <param name="message">The action needed before publication can proceed.</param>
    /// <param name="innerException">The filesystem ownership conflict.</param>
    public CookOutputBusyException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
