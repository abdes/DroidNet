// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Utility class for serializing and deserializing scenes using high-performance source-generated JSON.
/// </summary>
public static class SceneSerializer
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
        RuntimeHelpers.RunClassConstructor(typeof(DirectionalLightComponent).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(PointLightComponent).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(SpotLightComponent).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(LocalFogVolumeComponent).TypeHandle);
        RuntimeHelpers.RunClassConstructor(typeof(ScriptingComponent).TypeHandle);

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
    public static async Task<Scene> DeserializeAsync(Stream stream)
    {
        EnsureFactories();
        var data = await JsonSerializer.DeserializeAsync(stream, SceneJsonContext.Default.SceneData)
            .ConfigureAwait(false) ?? throw new JsonException("Failed to deserialize scene data.");

        // Use the canonical factory to set required init-only properties then hydrate.
        return Scene.CreateAndHydrate(data);
    }

    /// <summary>
    /// Deserializes scene data into an existing scene instance.
    /// </summary>
    /// <param name="stream">The stream containing the JSON scene data.</param>
    /// <param name="targetScene">The existing scene to populate with deserialized data.</param>
    /// <exception cref="JsonException">Thrown if deserialization fails.</exception>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public static async Task DeserializeIntoAsync(Stream stream, Scene targetScene)
    {
        var data = await JsonSerializer.DeserializeAsync(stream, SceneJsonContext.Default.SceneData)
            .ConfigureAwait(false);

        if (data == null)
        {
            return;
        }

        targetScene.Hydrate(data);
    }

    /// <summary>
    /// Serializes a scene to a stream.
    /// </summary>
    /// <param name="stream">The stream to write the JSON scene data to.</param>
    /// <param name="scene">The scene to serialize.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public static async Task SerializeAsync(Stream stream, Scene scene)
    {
        var data = scene.Dehydrate();
        await JsonSerializer.SerializeAsync(stream, data, SceneJsonContext.Default.SceneData)
            .ConfigureAwait(false);
    }

    /// <summary>
    /// Reads the identity a scene file holds, without reading the scene.
    /// </summary>
    /// <param name="json">The scene file's bytes, with or without a UTF-8 byte order mark.</param>
    /// <returns>The scene's id, or <see langword="null"/> when the bytes are not a scene with a non-empty id.</returns>
    public static SceneId? ReadId(ReadOnlySpan<byte> json)
    {
        var content = json.StartsWith(Encoding.UTF8.Preamble) ? json[Encoding.UTF8.Preamble.Length..] : json;
        try
        {
            using var document = JsonDocument.Parse(content.ToArray());
            return document.RootElement.ValueKind == JsonValueKind.Object
                && document.RootElement.TryGetProperty(nameof(SceneData.Id), out var id)
                && id.ValueKind == JsonValueKind.String
                && id.TryGetGuid(out var value)
                && value != Guid.Empty
                    ? SceneId.FromValue(value)
                    : null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    /// <summary>
    /// Registers the component and slot factories that hydration looks types up in.
    /// </summary>
    internal static void EnsureFactories() => _ = FactoriesInitialized.Value;
}
