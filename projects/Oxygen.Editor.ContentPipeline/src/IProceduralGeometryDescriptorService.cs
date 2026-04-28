// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Creates derived geometry descriptors for editor-generated geometry.
/// </summary>
public interface IProceduralGeometryDescriptorService
{
    /// <summary>
    /// Ensures descriptor inputs exist for the given generated geometry URIs.
    /// </summary>
    /// <param name="scope">The content cook scope.</param>
    /// <param name="geometryUris">The generated geometry URIs.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The generated descriptor inputs.</returns>
    public Task<IReadOnlyList<ContentCookInput>> EnsureDescriptorsAsync(
        ContentCookScope scope,
        IReadOnlyList<Uri> geometryUris,
        CancellationToken cancellationToken);
}
