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
/// (<see cref="AssetFileType.ReferenceFields"/>, <see cref="SceneReferenceFields"/>). A reference resolves against the
/// file's identity and is respelled from it. Any other string is left alone. Geometry buffers are file-relative paths;
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
    /// <returns>Whether the file is an authored descriptor, an input file or a model's preview.</returns>
    public static bool IsReferencingSource(string path)
        => AssetFileTypes.Find(path)?.Category is AssetFileCategory.Descriptor or AssetFileCategory.Input or AssetFileCategory.ModelPreview or AssetFileCategory.BufferContainer;

    /// <summary>Reads the asset identities a descriptor references in its reference fields, each with its field.</summary>
    /// <param name="path">The descriptor.</param>
    /// <param name="identity">The descriptor's identity.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <returns>Each reference, in the order of its fields.</returns>
    /// <exception cref="JsonException">The descriptor is not JSON.</exception>
    /// <exception cref="AssetFormatException">The descriptor is a texture its reader refuses.</exception>
    public static IReadOnlyList<AuthoredReference> ReadReferences(FileSystemPath path, AssetPath identity, ReadOnlyMemory<byte> content)
    {
        if (IsTexture(path))
        {
            // A texture references no asset; reading it checks that it is one.
            _ = TextureSourceReader.Read(content.Span, identity);
            return [];
        }

        var root = JsonNode.Parse(StripBom(content.Span));
        return [.. ReferenceFields(path).SelectMany(field => field.Select(root))
            .Select(node => TryParseReference(path, identity, node.GetValue<string>(), out var target) ? new AuthoredReference(node.GetPath(), target, node.GetValue<string>()) : null)
            .OfType<AuthoredReference>()];
    }

    /// <summary>Tests whether a file can produce buffers: a buffer container, or a geometry descriptor naming its own.</summary>
    /// <param name="path">The file path.</param>
    /// <returns>Whether the file can name buffer products.</returns>
    public static bool ProducesBuffers(string path)
        => path.EndsWith(AssetFileTypes.BufferContainer, StringComparison.Ordinal) || path.EndsWith(AssetFileTypes.GeometryDescriptor, StringComparison.Ordinal);

    /// <summary>Reads the buffers a file produces, whose references resolve as any of the file's do.</summary>
    /// <param name="file">The file's virtual path, which relative product names resolve against.</param>
    /// <param name="content">The file's bytes.</param>
    /// <returns>The identities of the products it names.</returns>
    /// <exception cref="JsonException">The file is not JSON.</exception>
    public static IReadOnlyList<AssetPath> ReadBufferProducts(AssetPath file, ReadOnlyMemory<byte> content)
    {
        ArgumentNullException.ThrowIfNull(file);
        return JsonNode.Parse(StripBom(content.Span))?["buffers"] is not JsonArray buffers
            ? []
            : [.. buffers.OfType<JsonObject>()
                .Select(static buffer => buffer["virtual_path"])
                .OfType<JsonValue>()
                .Select(name => name.TryGetValue<string>(out var text) && AssetPath.TryResolveReference(file, text, VirtualPathRuleSet.Cooked, out var product, out _) ? product : null)
                .OfType<AssetPath>()];
    }

    /// <summary>Reads the references of an engine format that the reference grammar refuses, each with its field.</summary>
    /// <param name="path">The descriptor.</param>
    /// <param name="identity">The descriptor's identity.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <returns>Each refused reference; none for a format that holds no references from its identity.</returns>
    /// <exception cref="JsonException">The descriptor is not JSON.</exception>
    public static IReadOnlyList<RefusedReference> ReadRefusedReferences(FileSystemPath path, AssetPath identity, ReadOnlyMemory<byte> content)
    {
        if (IsTexture(path))
        {
            return [];
        }

        var root = JsonNode.Parse(StripBom(content.Span));
        return [.. ReferenceFields(path).SelectMany(field => field.Select(root))
            .Where(node => !AssetFileTypes.Is(path.Name, AssetFileCategory.Input) || new InputActionReference(node.GetValue<string>()).IsReference)
            .Select(node => AssetPath.TryResolveReference(identity, node.GetValue<string>(), VirtualPathRuleSet.Authored, out _, out var error)
                ? null : new RefusedReference(node.GetPath(), node.GetValue<string>(), error.Value.ToCode()))
            .OfType<RefusedReference>()];
    }

    /// <summary>Renames an action or a mapping context inside its input source, keeping the rest of the file.</summary>
    /// <param name="content">The input source bytes.</param>
    /// <param name="rename">The rename.</param>
    /// <returns>The rewritten bytes.</returns>
    /// <exception cref="AssetFormatException">The source cannot be read or refuses the rename.</exception>
    public static byte[] RenameInputProduct(ReadOnlyMemory<byte> content, InputProductRename rename)
    {
        ArgumentNullException.ThrowIfNull(rename);
        var renamed = InputSourceWriter.RenameProduct(content.Span, rename.Product.Parent!, rename.GetExtension(), rename.CurrentName, rename.Name);
        return Serialize(renamed, content.Span);
    }

    /// <summary>Rewrites one reference field, keeping the rest of the file.</summary>
    /// <param name="path">The descriptor.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="retarget">The field and its new text.</param>
    /// <returns>The rewritten bytes.</returns>
    /// <exception cref="AssetRelocationException">The field no longer holds the text the rewrite was asked for.</exception>
    public static byte[] RetargetReference(FileSystemPath path, ReadOnlyMemory<byte> content, ReferenceRetarget retarget)
    {
        ArgumentNullException.ThrowIfNull(retarget);
        var root = JsonNode.Parse(StripBom(content.Span));
        var field = ReferenceFields(path).SelectMany(field => field.Select(root))
            .FirstOrDefault(node => string.Equals(node.GetPath(), retarget.Property, StringComparison.Ordinal)
                && string.Equals(node.GetValue<string>(), retarget.Written, StringComparison.Ordinal))
            ?? throw new AssetRelocationException($"'{path.Name}' changed since its references were checked. Check them again.");
        field.ReplaceWith(JsonValue.Create(retarget.Replacement));
        return Serialize(root!, content.Span);
    }

    /// <summary>Rewrites a descriptor for a relocation.</summary>
    /// <param name="project">The project that holds the descriptor.</param>
    /// <param name="path">The descriptor's current location.</param>
    /// <param name="content">The descriptor bytes.</param>
    /// <param name="map">The relocation.</param>
    /// <returns>The rewritten bytes, or null when nothing changes.</returns>
    public static byte[]? RewriteDescriptor(ProjectContext project, FileSystemPath path, ReadOnlyMemory<byte> content, RelocationMap map)
    {
        if (IsTexture(path))
        {
            return RewriteTexture(project, path, map.MapPath(path), content, map);
        }

        var root = JsonNode.Parse(StripBom(content.Span));
        var changed = RewriteReferences(project, path, map.MapPath(path), ReferenceFields(path).SelectMany(field => field.Select(root)), map);
        changed |= RewriteFileReferences(root, path, map);
        return changed ? Serialize(root!, content.Span) : null;
    }

    /// <summary>Reads the images a texture descriptor reads, resolved against its folder; a six-face cube's are its six
    /// faces.</summary>
    /// <param name="project">The project that holds the descriptor and its images.</param>
    /// <param name="path">The texture descriptor.</param>
    /// <param name="content">Its bytes.</param>
    /// <returns>The absolute image paths; none when the descriptor cannot be read, and none for a reference that names
    /// no file.</returns>
    public static IReadOnlyList<FileSystemPath> ReadTextureSources(ProjectContext project, FileSystemPath path, ReadOnlyMemory<byte> content)
    {
        if (IdentityOf(project, path) is not { } identity)
        {
            return [];
        }

        try
        {
            return [.. TextureSourceReader.Read(content.Span, identity)
                .ResolveImages(identity, project, image => project.FileSystem.File.Exists(image.FullPath))
                .Select(static image => image.File)
                .OfType<FileSystemPath>()];
        }
        catch (AssetFormatException)
        {
            return [];
        }
    }

    /// <summary>Keeps the file-relative sources of a copied descriptor valid, and gives a copied scene its own identity.</summary>
    /// <param name="project">The project that holds the descriptor.</param>
    /// <param name="sourcePath">The original descriptor.</param>
    /// <param name="targetPath">The copy's location.</param>
    /// <param name="content">The original bytes.</param>
    /// <param name="map">Maps the original identity and copied companions to the copy's.</param>
    /// <param name="closure">The assets copied because the copy needs them; the copy references those copies.</param>
    /// <returns>The copy's bytes.</returns>
    /// <exception cref="JsonException">The descriptor is not JSON.</exception>
    /// <exception cref="AssetFormatException">The descriptor is a texture its reader refuses.</exception>
    public static byte[] RewriteCopy(ProjectContext project, FileSystemPath sourcePath, FileSystemPath targetPath, ReadOnlyMemory<byte> content, RelocationMap map, RelocationMap closure)
    {
        if (IsTexture(sourcePath))
        {
            return RewriteTexture(project, sourcePath, targetPath, content, map) ?? content.ToArray();
        }

        var root = JsonNode.Parse(StripBom(content.Span));

        // A copy references what the original does, spelled from where the copy is.
        var changed = RewriteReferences(project, sourcePath, targetPath, ReferenceFields(sourcePath).SelectMany(field => field.Select(root)), closure);

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

    // The editor authors scenes, and a model's preview, in its own format; the engine's formats are described by their
    // file types.
    private static IReadOnlyList<AssetReferenceField> ReferenceFields(FileSystemPath path)
        => path.Name.EndsWith(AssetFileTypes.SceneDescriptor, StringComparison.Ordinal) || AssetFileTypes.Is(path.Name, AssetFileCategory.ModelPreview)
            ? SceneReferenceFields.All
            : [.. (AssetFileTypes.Find(path.Name)?.ReferenceFields ?? []).Where(static field => !field.IsFile)];

    private static AssetPath? IdentityOf(ProjectContext project, FileSystemPath path)
        => project.FindByFilePath(path)?.Identity;

    // A reference follows its target, and is spelled from where the referring file now is.
    private static bool RewriteReferences(ProjectContext project, FileSystemPath path, FileSystemPath target, IEnumerable<JsonValue> references, RelocationMap? map)
    {
        if (IdentityOf(project, path) is not { } anchor)
        {
            return false;
        }

        var newAnchor = IdentityOf(project, target) ?? anchor;
        var changed = false;
        foreach (var node in references.ToArray())
        {
            if (!TryParseReference(path, anchor, node.GetValue<string>(), out var identity))
            {
                continue;
            }

            var mapped = map?.MapIdentity(identity) ?? identity;
            if (mapped != identity || newAnchor != anchor)
            {
                node.ReplaceWith(JsonValue.Create(mapped.ToReference(newAnchor)));
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
        var nodes = ProducesBuffers(path.Name) && descriptor["buffers"] is JsonArray buffers
            ? buffers.OfType<JsonObject>().Select(static buffer => buffer["source"])
            : [];
        return [.. nodes.OfType<JsonValue>().Where(static node => node.TryGetValue<string>(out var value) && !string.IsNullOrWhiteSpace(value))];
    }

    private static bool IsTexture(FileSystemPath path) => path.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal);

    // A texture keeps its images, as the descriptor spells them, pointing at their files. An image in another mount does
    // not move with it.
    private static byte[]? RewriteTexture(ProjectContext project, FileSystemPath path, FileSystemPath target, ReadOnlyMemory<byte> content, RelocationMap map)
    {
        if (IdentityOf(project, path) is not { } identity)
        {
            return null;
        }

        var texture = TextureSourceReader.Read(content.Span, identity);
        var folder = path.Parent!;
        var newFolder = target.Parent!;
        var changed = false;
        string Relocate(string image)
        {
            var replacement = image.StartsWith('/') ? null : RelocateFile(image, folder, newFolder, map);
            changed |= replacement is not null;
            return replacement ?? image;
        }

        var relocated = texture with
        {
            Source = Relocate(texture.Source),
            Sources = [.. texture.Sources.Select(mapping => mapping with { File = Relocate(mapping.File) })],
        };
        return changed ? Serialize(TextureSourceWriter.ToJson(relocated, IdentityOf(project, target) ?? identity), content.Span) : null;
    }

    // A reference names an asset; a mount is not an asset, and an input mapping's local name is no reference.
    private static bool TryParseReference(FileSystemPath path, AssetPath anchor, string value, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out AssetPath? identity)
    {
        identity = null;
        if (AssetFileTypes.Is(path.Name, AssetFileCategory.Input) && !new InputActionReference(value).IsReference)
        {
            return false;
        }

        return AssetPath.TryResolveReference(anchor, value, VirtualPathRuleSet.Authored, out identity, out _) && !identity.IsMount;
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
}
