// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization.Metadata;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Reads and writes the text a scene file holds. A file spells its references from the scene's identity; everything past
/// this class holds asset paths.
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
        RuntimeHelpers.RunClassConstructor(typeof(SceneInstanceComponent).TypeHandle);

        // Add other types as needed
        return true;
    });

    /// <summary>Reads a scene file into a scene.</summary>
    /// <param name="file">The file's bytes, with or without a UTF-8 byte order mark.</param>
    /// <param name="identity">The scene's identity, which its references are read from.</param>
    /// <returns>A new scene.</returns>
    /// <exception cref="JsonException">The file is not a scene, or writes a reference the grammar refuses.</exception>
    public static Scene Read(ReadOnlySpan<byte> file, AssetPath identity)
    {
        EnsureFactories();
        return Scene.CreateAndHydrate(Read(file, identity, SceneJsonContext.Default.SceneData));
    }

    /// <summary>Reads what a scene file holds, its references resolved to their asset paths.</summary>
    /// <typeparam name="T">The record the file is read as.</typeparam>
    /// <param name="file">The file's bytes, with or without a UTF-8 byte order mark.</param>
    /// <param name="identity">The scene's identity, which its references are read from.</param>
    /// <param name="type">The record's serialization contract.</param>
    /// <returns>The record.</returns>
    /// <exception cref="JsonException">The file is not a scene, or writes a reference the grammar refuses.</exception>
    public static T Read<T>(ReadOnlySpan<byte> file, AssetPath identity, JsonTypeInfo<T> type)
        where T : SceneData
    {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(type);
        var content = file.StartsWith(Encoding.UTF8.Preamble) ? file[Encoding.UTF8.Preamble.Length..] : file;
        var root = JsonNode.Parse(content) ?? throw new JsonException("The file holds no scene.");
        foreach (var reference in References(root))
        {
            var written = reference.GetValue<string>();
            if (!AssetPath.TryResolveReference(identity, written, VirtualPathRuleSet.Authored, out var asset, out var error))
            {
                throw new JsonException($"{reference.GetPath()} writes '{written}', which the scene '{identity.VirtualPath}' cannot read: {error.Value.ToCode()}.");
            }

            reference.ReplaceWith(JsonValue.Create(asset.ToUriString()));
        }

        return root.Deserialize(type) ?? throw new JsonException("The file holds no scene.");
    }

    /// <summary>Gets the bytes of a scene's file.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="identity">The scene's identity, which its references are spelled from.</param>
    /// <returns>The file's content.</returns>
    public static byte[] Write(Scene scene, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return Write(scene.Dehydrate(), identity, SceneJsonContext.Default.SceneData);
    }

    /// <summary>Gets the bytes of a scene's file from what it holds, its asset paths spelled as references.</summary>
    /// <typeparam name="T">The record the file is written from.</typeparam>
    /// <param name="data">The record.</param>
    /// <param name="identity">The scene's identity, which its references are spelled from.</param>
    /// <param name="type">The record's serialization contract.</param>
    /// <returns>The file's content.</returns>
    public static byte[] Write<T>(T data, AssetPath identity, JsonTypeInfo<T> type)
        where T : SceneData
    {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(type);
        var root = JsonSerializer.SerializeToNode(data, type)!;
        foreach (var reference in References(root))
        {
            var asset = AssetPath.ParseUri(reference.GetValue<string>());
            reference.ReplaceWith(JsonValue.Create(asset.ToReference(identity)));
        }

        return JsonSerializer.SerializeToUtf8Bytes(root, new JsonSerializerOptions { WriteIndented = type.Options.WriteIndented, Encoder = type.Options.Encoder });
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
    /// Registers the component factories that hydration looks types up in.
    /// </summary>
    internal static void EnsureFactories() => _ = FactoriesInitialized.Value;

    private static JsonValue[] References(JsonNode root)
        => [.. SceneReferenceFields.All.SelectMany(field => field.Select(root))];
}
