// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Reads and rewrites the asset references of authored files. Descriptor references are whole JSON string
/// values naming an asset identity, either as a full <c>asset:///</c> URI or as a canonical virtual path;
/// texture sources and geometry buffers are file-relative paths; import sidecars are typed settings.
/// </summary>
internal static class AuthoredReferenceFormat
{
    private const string UriPrefix = "asset://";

    private static readonly string[] DescriptorExtensions =
        [".oscene.json", ".omat.json", ".ogeo.json", ".otex.json", ".opscene.json", ".ocshape.json", ".opmat.json"];

    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>Tests whether a file is an authored descriptor whose JSON may reference other assets.</summary>
    /// <param name="path">The file path.</param>
    /// <returns>Whether the file is a scene, material, geometry, texture or physics descriptor.</returns>
    public static bool IsDescriptor(string path)
        => DescriptorExtensions.Any(extension => path.EndsWith(extension, StringComparison.OrdinalIgnoreCase));

    /// <summary>Tests whether a file is a model import sidecar.</summary>
    /// <param name="path">The file path.</param>
    /// <returns>Whether the file is <c>&lt;model&gt;.import.json</c> for a glTF, GLB or FBX source.</returns>
    public static bool IsImportSettings(string path)
        => path.EndsWith(NativeSceneImportSettings.SidecarSuffix, StringComparison.OrdinalIgnoreCase)
            && Path.GetExtension(path[..^NativeSceneImportSettings.SidecarSuffix.Length]).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX";

    /// <summary>Reads the asset identities a descriptor references.</summary>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="mounts">The authoring mount names that identify virtual paths.</param>
    /// <returns>The distinct referenced identities.</returns>
    public static IReadOnlyList<string> ReadReferences(ReadOnlyMemory<byte> content, IReadOnlyCollection<string> mounts)
    {
        var root = JsonNode.Parse(StripBom(content.Span));
        var identities = new List<string>();
        Visit(root, value =>
        {
            if (TryParseIdentity(value, mounts, out var identity, out _))
            {
                identities.Add(identity);
            }

            return null;
        });
        return [.. identities.Distinct(StringComparer.OrdinalIgnoreCase)];
    }

    /// <summary>Rewrites a descriptor for a relocation.</summary>
    /// <param name="path">The descriptor's current location.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="map">The relocation.</param>
    /// <param name="mounts">The authoring mount names that identify virtual paths.</param>
    /// <returns>The rewritten bytes, or null when nothing changes.</returns>
    public static byte[]? RewriteDescriptor(string path, ReadOnlyMemory<byte> content, RelocationMap map, IReadOnlyCollection<string> mounts)
    {
        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = false;
        Visit(root, value =>
        {
            if (!TryParseIdentity(value, mounts, out var identity, out var form) || map.MapIdentity(identity) is not { } mapped)
            {
                return null;
            }

            changed = true;
            return form.Format(mapped);
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
        var bundle = Path.GetFullPath(Path.Combine(projectRoot, settings.BundleRoot));
        var newBundle = map.MapPath(bundle);
        string Remap(string relative)
            => Path.GetRelativePath(newBundle, map.MapPath(settings.ResolveFile(projectRoot, relative))).Replace('\\', '/');

        var files = settings.Files.Select(Remap).ToArray();
        var primary = Remap(settings.PrimaryRelativePath);
        var group = "/" + settings.MountPoint + "/" + RelocationPaths.ImportTypeFolders[0] + "/" + settings.OutputDirectory;
        var mappedGroup = map.MapIdentity(group);
        var output = mappedGroup is null ? settings.OutputDirectory : mappedGroup[(group.Length - settings.OutputDirectory.Length)..];
        var bundleRoot = string.Equals(newBundle, bundle, StringComparison.Ordinal)
            ? settings.BundleRoot
            : Path.GetRelativePath(Path.GetFullPath(projectRoot), newBundle).Replace('\\', '/');
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
    public static IReadOnlyList<string> ReadTextureSources(string path, ReadOnlyMemory<byte> content)
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

        var folder = Path.GetDirectoryName(Path.GetFullPath(path))!;
        return [.. EnumerateFileReferences(root, path).Select(node => node.GetValue<string>()).Select(value => Path.GetFullPath(Path.IsPathRooted(value) ? value : Path.Combine(folder, value)))];
    }

    /// <summary>Gives a copied descriptor its new identity and keeps its file-relative sources valid.</summary>
    /// <param name="sourcePath">The original descriptor.</param>
    /// <param name="targetPath">The copy's location.</param>
    /// <param name="content">The original bytes.</param>
    /// <param name="map">Maps the original identity and copied companions to the copy's.</param>
    /// <param name="mounts">The authoring mount names.</param>
    /// <returns>The copy's bytes.</returns>
    public static byte[] RewriteCopy(string sourcePath, string targetPath, ReadOnlyMemory<byte> content, RelocationMap map, IReadOnlyCollection<string> mounts)
    {
        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = false;
        if (root is JsonObject descriptor && descriptor["virtual_path"] is JsonValue own && own.TryGetValue<string>(out var value)
            && TryParseIdentity(value, mounts, out var identity, out var form) && map.MapIdentity(identity) is { } mapped)
        {
            descriptor["virtual_path"] = form.Format(mapped);
            changed = true;
        }

        changed |= RewriteFileReferences(root, sourcePath, map, targetPath);
        return changed ? Serialize(root!, content.Span) : content.ToArray();
    }

    /// <summary>Maps one reference value through an identity mapping, keeping its form.</summary>
    /// <param name="value">A full <c>asset:///</c> URI or a canonical virtual path.</param>
    /// <param name="mapIdentity">Returns the new identity, or null when the identity does not move.</param>
    /// <returns>The mapped reference, or null when the value is not a reference or does not move.</returns>
    public static string? MapReference(string value, Func<string, string?> mapIdentity)
        => TryParseIdentity(value, mounts: null, out var identity, out var form) && mapIdentity(identity) is { } mapped ? form.Format(mapped) : null;

    private static bool RewriteFileReferences(JsonNode? root, string path, RelocationMap map, string? targetPath = null)
    {
        if (root is not JsonObject descriptor)
        {
            return false;
        }

        var folder = Path.GetDirectoryName(Path.GetFullPath(path))!;
        var newFolder = Path.GetDirectoryName(targetPath is null ? map.MapPath(path) : Path.GetFullPath(targetPath))!;
        var changed = false;
        foreach (var node in EnumerateFileReferences(descriptor, path))
        {
            var value = node.GetValue<string>();
            var rooted = Path.IsPathRooted(value);
            var absolute = Path.GetFullPath(rooted ? value : Path.Combine(folder, value));
            var moved = map.MapPath(absolute);
            if (string.Equals(moved, absolute, StringComparison.Ordinal) && string.Equals(newFolder, folder, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var replacement = rooted ? moved : Path.GetRelativePath(newFolder, moved).Replace('\\', '/');
            if (!string.Equals(replacement, value, StringComparison.Ordinal))
            {
                node.ReplaceWith(JsonValue.Create(replacement));
                changed = true;
            }
        }

        return changed;
    }

    private static JsonValue[] EnumerateFileReferences(JsonObject descriptor, string path)
    {
        var nodes = new List<JsonNode?>();
        if (path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase))
        {
            nodes.Add(descriptor["source"]);
            if (descriptor["sources"] is JsonArray sources)
            {
                nodes.AddRange(sources.OfType<JsonObject>().Select(static source => source["file"]));
            }
        }
        else if (path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase) && descriptor["buffers"] is JsonArray buffers)
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

    private static bool TryParseIdentity(string value, IReadOnlyCollection<string>? mounts, out string identity, out IdentityForm form)
    {
        identity = string.Empty;
        form = default;
        string path;
        var isUri = value.StartsWith(UriPrefix, StringComparison.OrdinalIgnoreCase);
        if (isUri)
        {
            path = value[UriPrefix.Length..];
            if (!path.StartsWith('/'))
            {
                return false;
            }
        }
        else if (value.StartsWith('/') && !value.StartsWith("//", StringComparison.Ordinal))
        {
            path = value;
        }
        else
        {
            return false;
        }

        var escaped = isUri && path.Contains('%', StringComparison.Ordinal);
        var virtualPath = escaped ? Uri.UnescapeDataString(path) : path;
        var segments = virtualPath.Split('/');
        if (segments.Length < 3 || segments.Skip(1).Any(static segment => segment.Length == 0)
            || (mounts?.Contains(segments[1], StringComparer.OrdinalIgnoreCase) == false))
        {
            return false;
        }

        identity = RelocationPaths.ToIdentity(virtualPath);
        form = new(isUri, escaped, virtualPath.Length != identity.Length);
        return true;
    }

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

    /// <summary>How a reference spells an identity, so a rewrite keeps the same form.</summary>
    /// <param name="IsUri">Whether the value is an <c>asset:///</c> URI rather than a virtual path.</param>
    /// <param name="IsEscaped">Whether the URI percent-escapes its segments.</param>
    /// <param name="HasJsonSuffix">Whether the value names the authored <c>.json</c> file.</param>
    private readonly record struct IdentityForm(bool IsUri, bool IsEscaped, bool HasJsonSuffix)
    {
        public string Format(string identity)
        {
            var path = this.HasJsonSuffix ? identity + ".json" : identity;
            return !this.IsUri ? path : UriPrefix + (this.IsEscaped ? string.Join('/', path.Split('/').Select(Uri.EscapeDataString)) : path);
        }
    }
}
