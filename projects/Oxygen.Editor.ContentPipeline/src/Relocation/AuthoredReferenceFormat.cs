// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Reads and rewrites the asset references of authored files. Each format names its reference fields
/// (<see cref="AssetFileType.ReferenceFields"/>, <see cref="SceneReferenceFields"/>) and the field naming its own
/// identity (<see cref="AssetFileType.IdentityField"/>); a reference is an asset URI or a canonical virtual path, each
/// spelled by <see cref="AssetPath"/>, and any other string is left alone. Geometry buffers are file-relative paths;
/// textures and import sidecars are read and written through their typed formats.
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

    /// <summary>Reads the asset identities a descriptor references in its reference fields.</summary>
    /// <param name="path">The descriptor.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <returns>The distinct referenced identities.</returns>
    /// <exception cref="JsonException">The descriptor is not JSON.</exception>
    /// <exception cref="AssetFormatException">The descriptor is a texture its reader refuses.</exception>
    public static IReadOnlyList<AssetPath> ReadReferences(FileSystemPath path, ReadOnlyMemory<byte> content)
    {
        if (IsTexture(path))
        {
            // A texture references no asset; reading it checks that it is one.
            _ = TextureSourceReader.Read(content.Span);
            return [];
        }

        var root = JsonNode.Parse(StripBom(content.Span));
        return [.. ReferenceFields(path).SelectMany(field => field.Select(root))
            .Select(static node => TryParseReference(node.GetValue<string>(), out var identity, out _) ? identity : null)
            .OfType<AssetPath>()
            .Distinct()];
    }

    /// <summary>Renames an action or a mapping context inside its input source, keeping the rest of the file.</summary>
    /// <param name="content">The input source bytes.</param>
    /// <param name="rename">The rename.</param>
    /// <returns>The rewritten bytes.</returns>
    /// <exception cref="AssetFormatException">The source cannot be read or refuses the rename.</exception>
    public static byte[] RenameInputProduct(ReadOnlyMemory<byte> content, InputProductRename rename)
    {
        ArgumentNullException.ThrowIfNull(rename);
        var renamed = InputSourceWriter.RenameProduct(content.Span, rename.Extension, rename.CurrentName, rename.Name);
        return Serialize(renamed, content.Span);
    }

    /// <summary>Rewrites a descriptor for a relocation.</summary>
    /// <param name="path">The descriptor's current location.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="map">The relocation.</param>
    /// <returns>The rewritten bytes, or null when nothing changes.</returns>
    public static byte[]? RewriteDescriptor(FileSystemPath path, ReadOnlyMemory<byte> content, RelocationMap map)
    {
        if (IsTexture(path))
        {
            return RewriteTexture(path, map.MapPath(path), content, map);
        }

        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = RewriteReferences(IdentityField(path).Concat(ReferenceFields(path)).SelectMany(field => field.Select(root)), map);
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
        var bundleRoot = string.Equals(newBundle.FullPath, bundle.FullPath, StringComparison.Ordinal)
            ? settings.BundleRoot
            : newBundle.GetPathBelow(root);
        return string.Equals(bundleRoot, settings.BundleRoot, StringComparison.Ordinal)
            && string.Equals(primary, settings.PrimaryRelativePath, StringComparison.Ordinal)
            && files.SequenceEqual(settings.Files, StringComparer.Ordinal)
            ? settings
            : settings with { BundleRoot = bundleRoot, PrimaryRelativePath = primary, Files = [.. files] };
    }

    /// <summary>Reads the images a texture descriptor reads, resolved against its folder; a six-face cube's are its six
    /// faces.</summary>
    /// <param name="project">The project that holds the descriptor and its images.</param>
    /// <param name="path">The texture descriptor.</param>
    /// <param name="content">Its bytes.</param>
    /// <returns>The absolute image paths; none when the descriptor cannot be read.</returns>
    public static IReadOnlyList<FileSystemPath> ReadTextureSources(ProjectContext project, FileSystemPath path, ReadOnlyMemory<byte> content)
    {
        try
        {
            return TextureSourceReader.Read(content.Span).ResolveImages(path, image => project.FileSystem.File.Exists(image.FullPath));
        }
        catch (AssetFormatException)
        {
            return [];
        }
    }

    /// <summary>Gives a copied descriptor its new identity and keeps its file-relative sources valid.</summary>
    /// <param name="sourcePath">The original descriptor.</param>
    /// <param name="targetPath">The copy's location.</param>
    /// <param name="content">The original bytes.</param>
    /// <param name="map">Maps the original identity and copied companions to the copy's.</param>
    /// <returns>The copy's bytes.</returns>
    /// <exception cref="JsonException">The descriptor is not JSON.</exception>
    /// <exception cref="AssetFormatException">The descriptor is a texture its reader refuses.</exception>
    public static byte[] RewriteCopy(FileSystemPath sourcePath, FileSystemPath targetPath, ReadOnlyMemory<byte> content, RelocationMap map)
    {
        if (IsTexture(sourcePath))
        {
            return RewriteTexture(sourcePath, targetPath, content, map) ?? content.ToArray();
        }

        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = RewriteReferences(IdentityField(sourcePath).SelectMany(field => field.Select(root)), map);

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

    // The editor authors scenes in its own format; the engine's formats are described by their file types.
    private static IReadOnlyList<AssetReferenceField> ReferenceFields(FileSystemPath path)
        => path.Name.EndsWith(AssetFileTypes.SceneDescriptor, StringComparison.Ordinal)
            ? SceneReferenceFields.All
            : AssetFileTypes.Find(path.Name)?.ReferenceFields ?? [];

    private static AssetReferenceField[] IdentityField(FileSystemPath path)
        => AssetFileTypes.Find(path.Name)?.IdentityField is { } field ? [new(field)] : [];

    private static bool RewriteReferences(IEnumerable<JsonValue> references, RelocationMap map)
    {
        var changed = false;
        foreach (var node in references.ToArray())
        {
            if (TryParseReference(node.GetValue<string>(), out var identity, out var isUri) && map.MapIdentity(identity) is { } mapped)
            {
                node.ReplaceWith(JsonValue.Create(Format(mapped, isUri)));
                changed = true;
            }
        }

        return changed;
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
            if (RelocateFile(node.GetValue<string>(), folder, newFolder, map) is { } replacement)
            {
                node.ReplaceWith(JsonValue.Create(replacement));
                changed = true;
            }
        }

        return changed;
    }

    // A file-relative reference keeps naming its file, wherever the file and the referring descriptor go.
    private static string? RelocateFile(string value, FileSystemPath folder, FileSystemPath newFolder, RelocationMap map)
    {
        var rooted = Path.IsPathRooted(value);
        var absolute = rooted ? FileSystemPath.From(value) : folder.Combine(value);
        if (!map.IsMoved(absolute) && newFolder == folder)
        {
            return null;
        }

        var moved = map.MapPath(absolute);
        var replacement = rooted ? moved.FullPath : Path.GetRelativePath(newFolder.FullPath, moved.FullPath).Replace('\\', '/');
        return string.Equals(replacement, value, StringComparison.Ordinal) ? null : replacement;
    }

    private static JsonValue[] EnumerateFileReferences(JsonObject descriptor, FileSystemPath path)
    {
        var nodes = path.Name.EndsWith(AssetFileTypes.GeometryDescriptor, StringComparison.Ordinal) && descriptor["buffers"] is JsonArray buffers
            ? buffers.OfType<JsonObject>().Select(static buffer => buffer["uri"])
            : [];
        return [.. nodes.OfType<JsonValue>().Where(static node => node.TryGetValue<string>(out var value) && !string.IsNullOrWhiteSpace(value))];
    }

    private static bool IsTexture(FileSystemPath path) => path.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal);

    // A texture follows its own identity and keeps its images, as the descriptor spells them, pointing at their files.
    private static byte[]? RewriteTexture(FileSystemPath path, FileSystemPath target, ReadOnlyMemory<byte> content, RelocationMap map)
    {
        var texture = TextureSourceReader.Read(content.Span);
        var folder = path.Parent!;
        var newFolder = target.Parent!;
        var changed = false;
        string Relocate(string image)
        {
            var replacement = RelocateFile(image, folder, newFolder, map);
            changed |= replacement is not null;
            return replacement ?? image;
        }

        var virtualPath = texture.VirtualPath is { } own && map.MapIdentity(own) is { } mapped ? mapped : texture.VirtualPath;
        changed |= virtualPath != texture.VirtualPath;
        var relocated = texture with
        {
            VirtualPath = virtualPath,
            Source = Relocate(texture.Source),
            Sources = [.. texture.Sources.Select(mapping => mapping with { File = Relocate(mapping.File) })],
        };
        return changed ? Serialize(TextureSourceWriter.ToJson(relocated), content.Span) : null;
    }

    // A reference names an asset, as an asset URI or a virtual path; a mount is not an asset.
    private static bool TryParseReference(string value, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out AssetPath? identity, out bool isUri)
    {
        isUri = value.StartsWith(AssetPath.UriScheme + ":", StringComparison.OrdinalIgnoreCase);
        var parsed = isUri ? AssetPath.TryParseUri(value, out identity) : AssetPath.TryParse(value, out identity);
        return parsed && !identity!.IsMount;
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
