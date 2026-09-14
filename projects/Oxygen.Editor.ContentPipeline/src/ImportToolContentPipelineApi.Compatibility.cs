// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Protects the compatible tool set through owned worker termination and drain.</summary>
public sealed partial class ImportToolContentPipelineApi
{
    private static Task ReleaseAfterWorkerDrainAsync(Task drain, string inputPath, NativeArtifactLease? artifacts, string? additionalPath = null, FileStream? additionalLease = null)
        => drain.ContinueWith(
            async completed =>
            {
                _ = completed.Exception;
                if (additionalLease is not null)
                {
                    await additionalLease.DisposeAsync().ConfigureAwait(false);
                }

                if (artifacts is not null)
                {
                    await artifacts.DisposeAsync().ConfigureAwait(false);
                }

                TryDeleteFile(inputPath);
                if (additionalPath is not null)
                {
                    TryDeleteFile(additionalPath);
                }
            },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default).Unwrap();

    private string GetCompatibleToolPath(NativeArtifactLease artifacts)
    {
        var compatible = artifacts.GetPath(NativeArtifactInventory.ImportToolId);
        return string.Equals(Path.GetFullPath(this.toolLocator.GetImportToolPath()), compatible, StringComparison.OrdinalIgnoreCase)
            ? compatible : throw new InvalidOperationException("The selected import tool is outside the compatible artifact set.");
    }
}
