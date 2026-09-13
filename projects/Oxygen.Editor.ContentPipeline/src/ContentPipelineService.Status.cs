// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Exposes read-only cook facts through the existing shared pipeline service.</summary>
public sealed partial class ContentPipelineService
{
    /// <inheritdoc />
    public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default)
        => new AssetCookStatusReader(
            cookDocuments,
            this.publication,
            this.nativeCompatibility,
            provenanceFiles ?? new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()))
            .ReadAsync(project, assetUris, cancellationToken);
}
