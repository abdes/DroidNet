// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Text.Json;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Utility class for serializing and deserializing scenes using high-performance source-generated JSON.
/// </summary>
/// <param name="project">The project that owns the scenes being serialized/deserialized.</param>
public class SceneSerializer(IProject project)
{
    private static readonly Lazy<bool> FactoriesInitialized = new(() =>
    {
        // Scene game objects
        RuntimeHelpers.RunClassConstructor(typeof(GameObject).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(SceneNode).TypeHandle);

        // Components
        RuntimeHelpers.RunClassConstructor(typeof(GeometryComponent).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(PerspectiveCamera).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(OrthographicCamera).TypeHandle);

        // Slots
        RuntimeHelpers.RunClassConstructor(typeof(RenderingSlot).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(LightingSlot).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(MaterialsSlot).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(LevelOfDetailSlot).TypeHandle);

        // Add other types as needed
        return true;
    });

    /// <summary>
    /// Deserializes a scene from a stream.
    /// </summary>
    /// <param name="stream">The stream containing the JSON scene data.</param>
    /// <returns>A new <see cref="Scene"/> instance populated with the deserialized data.</returns>
    /// <exception cref="JsonException">Thrown if deserialization fails.</exception>
    public async Task<Scene> DeserializeAsync(Stream stream)
    {
        _ = FactoriesInitialized.Value; // Ensure factories are registered

        var data = await JsonSerializer.DeserializeAsync(stream, SceneJsonContext.Default.SceneData)
            .ConfigureAwait(false) ?? throw new JsonException("Failed to deserialize scene data.");

        // Use the canonical factory to set required init-only properties then hydrate.
        var scene = Scene.CreateAndHydrate(project, data);

        // Preserve editor-only explorer layout on the returned Scene instance so UI can read it.
        scene.ExplorerLayout = data.ExplorerLayout;
        return scene;
    }

    /// <summary>
    /// Deserializes scene data into an existing scene instance.
    /// </summary>
    /// <param name="stream">The stream containing the JSON scene data.</param>
    /// <param name="targetScene">The existing scene to populate with deserialized data.</param>
    /// <exception cref="JsonException">Thrown if deserialization fails.</exception>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1822:Mark members as static", Justification = "keep API consistent - all methods are instance methods")]
    public async Task DeserializeIntoAsync(Stream stream, Scene targetScene)
    {
        var data = await JsonSerializer.DeserializeAsync(stream, SceneJsonContext.Default.SceneData)
            .ConfigureAwait(false);

        if (data == null)
        {
            return;
        }

        // Hydrate scene content (nodes) but also transfer editor-only explorer layout
        targetScene.Hydrate(data);
        targetScene.ExplorerLayout = data.ExplorerLayout;
    }

    /// <summary>
    /// Serializes a scene to a stream.
    /// </summary>
    /// <param name="stream">The stream to write the JSON scene data to.</param>
    /// <param name="scene">The scene to serialize.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1822:Mark members as static", Justification = "keep API consistent - all methods are instance methods")]
    public async Task SerializeAsync(Stream stream, Scene scene)
    {
        var data = scene.Dehydrate();
        await JsonSerializer.SerializeAsync(stream, data, SceneJsonContext.Default.SceneData)
            .ConfigureAwait(false);
    }
}
