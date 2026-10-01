// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class CountingSourceRunner : IContentPipelineProcessRunner
{
    private readonly ContentPipelineProcessRunner inner = new();

    public int Count { get; private set; }

    public int ImportCount { get; private set; }

    public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
    {
        this.Count++;
        if (request.Arguments.Contains("batch", StringComparer.Ordinal))
        {
            this.ImportCount++;
        }

        return this.inner.RunAsync(request, cancellationToken);
    }
}
