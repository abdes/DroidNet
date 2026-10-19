// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Reads and rewrites the asset references of authored files. Descriptor references are whole JSON string
/// values naming an asset identity, either as an asset URI or as a canonical virtual path, each spelled by
/// <see cref="AssetPath"/>; texture sources and geometry buffers are file-relative paths; import sidecars are typed
/// settings.
/// </summary>
internal static class AuthoredReferenceFormat
{
    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>Tests whether a file is an authored source whose JSON may reference other assets.</summary>
    /// <param name="path">The file path.</param>
    /// <returns>Whether the file is an authored descriptor or an input file.</returns>
    public static bool IsReferencingSource(string path)
        => AssetFileTypes.Find(path)?.Category is AssetFileCategory.Descriptor or AssetFileCategory.Input;

    /// <summary>Tests whether a file is a model import sidecar.</summary>
    /// <param name="path">The file path.</param>
    /// <returns>Whether the file is <c>&lt;model&gt;.import.json</c> for a glTF, GLB or FBX source.</returns>
    public static bool IsImportSettings(string path)
        => AssetFileTypes.Is(path, AssetFileCategory.ImportSettings)
            && AssetFileTypes.Is(path[..^AssetFileTypes.ImportSettings.Length], AssetFileCategory.Model);

    /// <summary>Reads the asset identities a descriptor references.</summary>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="mounts">The authoring mount names that identify virtual paths.</param>
    /// <returns>The distinct referenced identities.</returns>
    public static IReadOnlyList<AssetPath> ReadReferences(ReadOnlyMemory<byte> content, IReadOnlyCollection<string> mounts)
    {
        var root = JsonNode.Parse(StripBom(content.Span));
        var identities = new List<AssetPath>();
        Visit(root, value =>
        {
            if (TryParseIdentity(value, mounts, out var identity, out _))
            {
                identities.Add(identity);
            }

            return null;
        });
        return [.. identities.Distinct()];
    }

    /// <summary>Rewrites a descriptor for a relocation.</summary>
    /// <param name="path">The descriptor's current location.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="map">The relocation.</param>
    /// <param name="mounts">The authoring mount names that identify virtual paths.</param>
    /// <returns>The rewritten bytes, or null when nothing changes.</returns>
    public static byte[]? RewriteDescriptor(FileSystemPath path, ReadOnlyMemory<byte> content, RelocationMap map, IReadOnlyCollection<string> mounts)
    {
        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = false;
        Visit(root, value =>
        {
            if (!TryParseIdentity(value, mounts, out var identity, out var isUri) || map.MapIdentity(identity) is not { } mapped)
            {
                return null;
            }

            changed = true;
            return Format(mapped, isUri);
        });
        changed |= RewriteFileReferences(root, path, map);
        return changed ? Serialize(root!, content.Span) : null;
    }

    /// <summary>Rewrites a model import sidecar for a relocation.</summary>
    /// <param name="projectRoot">The owning project.</param>
    /// <param name="content">The sidecar bytes.</param>
    /// <param name="map">The relocation.</param>
    /// <returns>The rewritten bytes, or null when nothing changes.</returns>
    public static byte[]? RewriteImportSettings(string projectRoot, ReadOnlyMemory<byte> content, RelocationMap map)
    {
        var settings = NativeSceneImportSettings.Parse(content);
        var updated = Relocate(projectRoot, settings, map);
        return updated == settings ? null : updated.ToBytes();
    }

    /// <summary>Returns the sidecar settings after a relocation.</summary>
    /// <param name="projectRoot">The owning project.</param>
    /// <param name="settings">The current settings.</param>
    /// <param name="map">The relocation.</param>
    /// <returns>The relocated settings; the same instance when nothing changes.</returns>
    public static NativeSceneImportSettings Relocate(string projectRoot, NativeSceneImportSettings settings, RelocationMap map)
    {
        var root = FileSystemPath.From(projectRoot);
        var bundle = root.Combine(settings.BundleRoot);
        var newBundle = map.MapPath(bundle);
        string Remap(string relative) => map.MapPath(FileSystemPath.From(settings.ResolveFile(projectRoot, relative))).GetPathBelow(newBundle);

        var files = settings.Files.Select(Remap).ToArray();
        var primary = Remap(settings.PrimaryRelativePath);
        var typeFolder = AssetPath.Create(settings.MountPoint, RelocationPaths.ImportTypeFolders[0]);
        var output = map.MapIdentity(typeFolder.Combine(settings.OutputDirectory)) is { } mappedGroup ? mappedGroup.GetPathBelow(typeFolder) : settings.OutputDirectory;
        var bundleRoot = string.Equals(newBundle.FullPath, bundle.FullPath, StringComparison.Ordinal)
            ? settings.BundleRoot
            : newBundle.GetPathBelow(root);
        return string.Equals(bundleRoot, settings.BundleRoot, StringComparison.Ordinal)
            && string.Equals(primary, settings.PrimaryRelativePath, StringComparison.Ordinal)
            && string.Equals(output, settings.OutputDirectory, StringComparison.Ordinal)
            && files.SequenceEqual(settings.Files, StringComparer.Ordinal)
            ? settings
            : settings with { BundleRoot = bundleRoot, PrimaryRelativePath = primary, Files = [.. files], OutputDirectory = output };
    }

    /// <summary>Reads a texture descriptor's relative image sources, resolved against its folder.</summary>
    /// <param name="path">The texture descriptor.</param>
    /// <param name="content">Its bytes.</param>
    /// <returns>The absolute image paths; none when the descriptor cannot be parsed.</returns>
    public static IReadOnlyList<FileSystemPath> ReadTextureSources(FileSystemPath path, ReadOnlyMemory<byte> content)
    {
        JsonNode? parsed;
        try
        {
            parsed = JsonNode.Parse(StripBom(content.Span));
        }
        catch (JsonException)
        {
            return [];
        }

        if (parsed is not JsonObject root)
        {
            return [];
        }

        var folder = path.Parent!;
        return [.. EnumerateFileReferences(root, path).Select(node => node.GetValue<string>()).Select(value => Path.IsPathRooted(value) ? FileSystemPath.From(value) : folder.Combine(value))];
    }

    /// <summary>Gives a copied descriptor its new identity and keeps its file-relative sources valid.</summary>
    /// <param name="sourcePath">The original descriptor.</param>
    /// <param name="targetPath">The copy's location.</param>
    /// <param name="content">The original bytes.</param>
    /// <param name="map">Maps the original identity and copied companions to the copy's.</param>
    /// <param name="mounts">The authoring mount names.</param>
    /// <returns>The copy's bytes.</returns>
    public static byte[] RewriteCopy(FileSystemPath sourcePath, FileSystemPath targetPath, ReadOnlyMemory<byte> content, RelocationMap map, IReadOnlyCollection<string> mounts)
    {
        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = false;
        if (root is JsonObject descriptor && descriptor["virtual_path"] is JsonValue own && own.TryGetValue<string>(out var value)
            && TryParseIdentity(value, mounts, out var identity, out var isUri) && map.MapIdentity(identity) is { } mapped)
        {
            descriptor["virtual_path"] = Format(mapped, isUri);
            changed = true;
        }

        // A copied scene is a scene of its own: it takes a new identity, and the name of its file.
        if (root is JsonObject scene && targetPath.Name.EndsWith(AssetFileTypes.SceneDescriptor, StringComparison.Ordinal))
        {
            scene["Id"] = Guid.NewGuid().ToString("D");
            scene["Name"] = targetPath.Name[..^AssetFileTypes.SceneDescriptor.Length];
            changed = true;
        }

        changed |= RewriteFileReferences(root, sourcePath, map, targetPath);
        return changed ? Serialize(root!, content.Span) : content.ToArray();
    }

    /// <summary>Reads the identity a scene file holds.</summary>
    /// <param name="content">The scene file's bytes.</param>
    /// <returns>The scene's id, or <see langword="null"/> when the file holds none.</returns>
    public static SceneId? ReadSceneId(ReadOnlyMemory<byte> content)
    {
        try
        {
            return JsonNode.Parse(StripBom(content.Span)) is JsonObject scene && scene["Id"] is JsonValue id
                && id.TryGetValue<string>(out var text) && Guid.TryParse(text, out var parsed)
                    ? SceneId.FromValue(parsed)
                    : null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static bool RewriteFileReferences(JsonNode? root, FileSystemPath path, RelocationMap map, FileSystemPath? targetPath = null)
    {
        if (root is not JsonObject descriptor)
        {
            return false;
        }

        var folder = path.Parent!;
        var newFolder = (targetPath ?? map.MapPath(path)).Parent!;
        var changed = false;
        foreach (var node in EnumerateFileReferences(descriptor, path))
        {
            var value = node.GetValue<string>();
            var rooted = Path.IsPathRooted(value);
            var absolute = rooted ? FileSystemPath.From(value) : folder.Combine(value);
            var moved = map.MapPath(absolute);
            if (!map.IsMoved(absolute) && newFolder == folder)
            {
                continue;
            }

            var replacement = rooted ? moved.FullPath : Path.GetRelativePath(newFolder.FullPath, moved.FullPath).Replace('\\', '/');
            if (!string.Equals(replacement, value, StringComparison.Ordinal))
            {
                node.ReplaceWith(JsonValue.Create(replacement));
                changed = true;
            }
        }

        return changed;
    }

    private static JsonValue[] EnumerateFileReferences(JsonObject descriptor, FileSystemPath path)
    {
        var nodes = new List<JsonNode?>();
        if (path.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal))
        {
            nodes.Add(descriptor["source"]);
            if (descriptor["sources"] is JsonArray sources)
            {
                nodes.AddRange(sources.OfType<JsonObject>().Select(static source => source["file"]));
            }
        }
        else if (path.Name.EndsWith(AssetFileTypes.GeometryDescriptor, StringComparison.Ordinal) && descriptor["buffers"] is JsonArray buffers)
        {
            nodes.AddRange(buffers.OfType<JsonObject>().Select(static buffer => buffer["uri"]));
        }

        return [.. nodes.OfType<JsonValue>().Where(static node => node.TryGetValue<string>(out var value) && !string.IsNullOrWhiteSpace(value))];
    }

    private static void Visit(JsonNode? node, Func<string, string?> rewrite)
    {
        switch (node)
        {
            case JsonObject value:
                foreach (var name in value.Select(static property => property.Key).ToArray())
                {
                    if (value[name] is JsonValue leaf && leaf.TryGetValue<string>(out var text))
                    {
                        if (rewrite(text) is { } replaced)
                        {
                            value[name] = replaced;
                        }
                    }
                    else
                    {
                        Visit(value[name], rewrite);
                    }
                }

                break;
            case JsonArray array:
                for (var index = 0; index < array.Count; index++)
                {
                    if (array[index] is JsonValue leaf && leaf.TryGetValue<string>(out var text))
                    {
                        if (rewrite(text) is { } replaced)
                        {
                            array[index] = replaced;
                        }
                    }
                    else
                    {
                        Visit(array[index], rewrite);
                    }
                }

                break;
        }
    }

    // A reference is an asset URI or a virtual path below one of the mounts, naming an asset rather than a mount.
    private static bool TryParseIdentity(string value, IReadOnlyCollection<string>? mounts, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out AssetPath? identity, out bool isUri)
    {
        isUri = value.StartsWith(AssetPath.UriScheme + ":", StringComparison.OrdinalIgnoreCase);
        var parsed = isUri ? AssetPath.TryParseUri(value, out identity) : AssetPath.TryParse(value, out identity);
        return parsed && !identity!.IsMount && mounts?.Contains(identity.Mount, StringComparer.Ordinal) != false;
    }

    private static string Format(AssetPath identity, bool isUri) => isUri ? identity.ToUriString() : identity.VirtualPath;

    private static byte[] Serialize(JsonNode root, ReadOnlySpan<byte> original)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(root, WriteOptions);
        var hasBom = original.StartsWith(Encoding.UTF8.Preamble);
        var newline = original.EndsWith("\n"u8);
        var result = new List<byte>(bytes.Length + 4);
        if (hasBom)
        {
            result.AddRange(Encoding.UTF8.Preamble.ToArray());
        }

        result.AddRange(bytes);
        if (newline)
        {
            result.Add((byte)'\n');
        }

        return [.. result];
    }

    private static ReadOnlySpan<byte> StripBom(ReadOnlySpan<byte> content)
        => content.StartsWith(Encoding.UTF8.Preamble) ? content[Encoding.UTF8.Preamble.Length..] : content;
}
