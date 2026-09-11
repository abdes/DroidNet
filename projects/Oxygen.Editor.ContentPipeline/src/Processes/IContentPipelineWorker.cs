// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Processes;

/// <summary>Owns one worker tree and its two independently drained output streams.</summary>
internal interface IContentPipelineWorker : IDisposable
{
    /// <summary>Gets completion after the entire owned process tree exits.</summary>
    public Task<int> Exit { get; }

    /// <summary>Gets the complete standard output, or its reader failure.</summary>
    public Task<string> StandardOutput { get; }

    /// <summary>Gets the complete standard error, or its reader failure.</summary>
    public Task<string> StandardError { get; }

    /// <summary>Stops the tree, returning false if it already exited naturally.</summary>
    /// <returns>Whether termination was issued before the job became empty.</returns>
    public bool Terminate();
}
