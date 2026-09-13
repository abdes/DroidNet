// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Shares saved-input and publication facts across browser, documents and pickers.</summary>
public interface IAssetCookStatusReader
{
    /// <summary>Inspects an authored asset set without cooking, recovering or publishing content.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="assetUris">Authored identities or engine built-ins whose optional published contributions are inspected.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The current cook-owned facts for each requested identity.</returns>
    public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default);
}
