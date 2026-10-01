// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using DroidNet.Storage;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class FailImportBatchRunner : IContentPipelineProcessRunner
{
    private readonly ContentPipelineProcessRunner inner = new();

    public bool Fail { get; set; } = true;

    public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        => this.Fail && request.Arguments.Contains("batch", StringComparer.Ordinal)
            ? Task.FromResult(new ContentPipelineProcessResult(1, string.Empty, "Simulated cook failure"))
            : this.inner.RunAsync(request, cancellationToken);
}
