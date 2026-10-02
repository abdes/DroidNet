// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reports deferred cleanup without changing the committed publication outcome.</summary>
public sealed partial class ContentPipelineService
{
    private async Task<ContentCookResult> ReclaimUnusedCookedOutputAsync(ContentCookOperation operation, ContentCookResult result)
    {
        IReadOnlyList<string> failures;
        try
        {
            failures = await this.publication.MaintainAfterWorkAsync(operation).ConfigureAwait(false);
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException or OperationCanceledException)
        {
            failures = [failure.Message];
        }

        if (failures.Count == 0)
        {
            return result;
        }

        return result with
        {
            Status = result.Status == OperationStatus.Succeeded ? OperationStatus.SucceededWithWarnings : result.Status,
            Diagnostics = [.. result.Diagnostics, new DiagnosticRecord
            {
                OperationId = operation.OperationId,
                Domain = FailureDomain.ContentPipeline,
                Severity = DiagnosticSeverity.Warning,
                Code = "Cook.CleanupDeferred",
                Message = "Some unused output will be reclaimed during a later maintenance pass.",
                TechnicalMessage = string.Join(Environment.NewLine, failures),
            }],
        };
    }
}
