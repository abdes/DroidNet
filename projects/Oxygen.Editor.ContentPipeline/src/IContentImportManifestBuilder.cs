// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Builds native content import manifests from resolved cook inputs.
/// </summary>
public interface IContentImportManifestBuilder
{
    /// <summary>Builds the initial model recipe before its source bundle has been retained.</summary>
    /// <param name="input">The logical source identity.</param>
    /// <param name="dependsOn">Other jobs required in the batch.</param>
    /// <param name="name">The reviewed model name.</param>
    /// <param name="layout">The reviewed output namespace.</param>
    /// <param name="provenance">The source's stable native slot identity.</param>
    /// <returns>The native recipe shared with retained-source cooking.</returns>
    public ContentImportJob BuildModelJob(ContentCookInput input, IReadOnlyList<string> dependsOn, string name, ContentImportLayout layout, Import.NativeMaterialSlotProvenance provenance);

    /// <summary>Builds one destination-free native recipe for discovery and execution.</summary>
    /// <param name="input">The logical source and output identity.</param>
    /// <param name="dependsOn">Other jobs required in the same batch.</param>
    /// <param name="modelSettings">Retained native settings for a foreign model source.</param>
    /// <returns>The native job with its complete recipe and layout.</returns>
    public ContentImportJob BuildJob(ContentCookInput input, IReadOnlyList<string> dependsOn, Import.NativeSceneImportSettings? modelSettings = null);

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
