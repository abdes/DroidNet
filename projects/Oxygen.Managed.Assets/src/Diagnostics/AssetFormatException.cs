// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Managed.Assets.Diagnostics;

/// <summary>
/// An asset file's content does not match its format. Thrown by the format readers and writers, for every problem of
/// the content, so a caller catches this type and reads <see cref="Problem"/>.
/// </summary>
[SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "A format problem always carries its problem and code.")]
public sealed class AssetFormatException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="AssetFormatException"/> class.</summary>
    /// <param name="problem">The problem of the content.</param>
    /// <param name="innerException">The failure that revealed it, if any.</param>
    public AssetFormatException(AssetProblem problem, Exception? innerException = null)
        : base((problem ?? throw new ArgumentNullException(nameof(problem))).Message, innerException)
    {
        this.Problem = problem;
    }

    /// <summary>Gets the problem of the content.</summary>
    public AssetProblem Problem { get; }
}
