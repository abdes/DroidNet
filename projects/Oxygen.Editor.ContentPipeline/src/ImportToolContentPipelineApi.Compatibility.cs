// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Protects the compatible tool set through owned worker termination and drain.</summary>
public sealed partial class ImportToolContentPipelineApi
{
    private static Task ReleaseAfterWorkerDrainAsync(Task drain, string inputPath, NativeArtifactLease? artifacts)
        => drain.ContinueWith(
            async completed =>
            {
                _ = completed.Exception;
                if (artifacts is not null)
                {
                    await artifacts.DisposeAsync().ConfigureAwait(false);
                }

                TryDeleteFile(inputPath);
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
