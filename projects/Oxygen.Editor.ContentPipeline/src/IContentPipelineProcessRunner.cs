// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Runs the bounded external content-pipeline tool process.
/// </summary>
public interface IContentPipelineProcessRunner
{
    /// <summary>
    /// Runs the requested process and captures its output.
    /// </summary>
    /// <param name="request">The process request.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The completed process result.</returns>
    public Task<ContentPipelineProcessResult> RunAsync(
        ContentPipelineProcessRequest request,
        CancellationToken cancellationToken);
}
