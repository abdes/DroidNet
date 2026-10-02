// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class ContextLeaseRunner : IContentPipelineProcessRunner
{
    private readonly ContentPipelineProcessRunner inner = new();

    public Func<Task>? BeforeBatch { get; set; }

    public Func<Task>? BeforeDependencyInspection { get; set; }

    public Func<Task>? BeforeInventoryInspection { get; set; }

    public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
    {
        if (this.BeforeInventoryInspection is not null && request.Arguments.Contains("inventory", StringComparer.Ordinal))
        {
            await this.BeforeInventoryInspection().ConfigureAwait(false);
        }

        if (this.BeforeDependencyInspection is not null && request.Arguments.Contains("dependencies", StringComparer.Ordinal))
        {
            await this.BeforeDependencyInspection().ConfigureAwait(false);
        }

        if (this.BeforeBatch is not null && request.Arguments.Contains("batch", StringComparer.Ordinal))
        {
            await this.BeforeBatch().ConfigureAwait(false);
        }

        return await this.inner.RunAsync(request, cancellationToken).ConfigureAwait(false);
    }
}
