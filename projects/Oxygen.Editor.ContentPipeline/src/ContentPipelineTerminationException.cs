// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Reports failed worker termination while retaining ownership until its I/O drains.
/// </summary>
[SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "Every termination failure must carry a retained worker drain; constructors without it would permit premature input cleanup.")]
public sealed class ContentPipelineTerminationException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="ContentPipelineTerminationException"/> class.</summary>
    /// <param name="cause">The native termination failure.</param>
    /// <param name="drainCompletion">The retained worker's tree and reader completion.</param>
    internal ContentPipelineTerminationException(Exception cause, Task drainCompletion)
        : base("The content worker could not be terminated. Its operation and inputs remain retained until the worker stops.", cause)
    {
        this.DrainCompletion = drainCompletion;
    }

    /// <summary>
    /// Gets completion of the owned worker tree and both output readers.
    /// Input cleanup and release of the project operation gate must await this task.
    /// </summary>
    public Task DrainCompletion { get; }
}
