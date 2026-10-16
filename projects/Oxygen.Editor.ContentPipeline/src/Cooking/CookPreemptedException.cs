// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>
/// Reports that a cook on change stopped before it published anything, so that a cook the user asked for could take
/// the writer; the caller cooks the source again if it is still stale.
/// </summary>
public sealed class CookPreemptedException : OperationCanceledException
{
    /// <summary>Initializes a new instance of the <see cref="CookPreemptedException"/> class.</summary>
    public CookPreemptedException()
        : base("The cook on change stopped so that other work on the project could run first.")
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookPreemptedException"/> class.</summary>
    /// <param name="message">The message that describes the stop.</param>
    public CookPreemptedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookPreemptedException"/> class.</summary>
    /// <param name="message">The message that describes the stop.</param>
    /// <param name="innerException">The cancellation that stopped the cook.</param>
    public CookPreemptedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
