// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Generates native scene descriptors from editor scene documents.
/// </summary>
public interface ISceneDescriptorGenerator
{
    /// <summary>
    /// Generates a native descriptor for <paramref name="scene"/>.
    /// </summary>
    /// <param name="scene">The scene document.</param>
    /// <param name="scope">The content cook scope.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The scene descriptor generation result.</returns>
    public Task<SceneDescriptorGenerationResult> GenerateAsync(
        Scene scene,
        ContentCookScope scope,
        CancellationToken cancellationToken);
}
