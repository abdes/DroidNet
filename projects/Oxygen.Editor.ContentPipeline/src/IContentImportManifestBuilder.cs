// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Builds native content import manifests from resolved cook inputs.
/// </summary>
public interface IContentImportManifestBuilder
{
    /// <summary>
    /// Builds the manifest for the resolved inputs in a single cook scope.
    /// </summary>
    /// <param name="scope">The resolved cook scope.</param>
    /// <returns>The native import manifest.</returns>
    public ContentImportManifest BuildManifest(ContentCookScope scope);

    /// <summary>
    /// Builds the manifest for a generated scene descriptor and its dependencies.
    /// </summary>
    /// <param name="scope">The content cook scope.</param>
    /// <param name="sceneDescriptor">The generated scene descriptor result.</param>
    /// <returns>The native import manifest.</returns>
    public ContentImportManifest BuildSceneManifest(
        ContentCookScope scope,
        SceneDescriptorGenerationResult sceneDescriptor);

    /// <summary>
    /// Builds the manifest for generated scene descriptors and their dependencies.
    /// </summary>
    /// <param name="scope">The content cook scope.</param>
    /// <param name="sceneDescriptors">The generated scene descriptor results.</param>
    /// <returns>The native import manifest.</returns>
    public ContentImportManifest BuildSceneManifests(
        ContentCookScope scope,
        IReadOnlyList<SceneDescriptorGenerationResult> sceneDescriptors);
}
