// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class FailingWorkerRunner(Exception exception) : IContentPipelineProcessRunner
{
    internal string? ManifestPath { get; private set; }

    public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
    {
        var index = request.Arguments.ToList().IndexOf("--manifest");
        this.ManifestPath = request.Arguments[index + 1];
        return Task.FromException<ContentPipelineProcessResult>(exception);
    }
}
