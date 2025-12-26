// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Assets.Filesystem;
using SixLabors.ImageSharp;

namespace Oxygen.Assets.Import.Textures;

/// <summary>
/// Imports standalone image sources (<c>.png</c>, <c>.jpg</c>, <c>.jpeg</c>, <c>.tga</c>) into a texture asset (<c>.otex</c>).
/// </summary>
public sealed class ImageTextureImporter : IAssetImporter
{
    private const string ImporterName = "Oxygen.Import.ImageTexture";

    public string Name => ImporterName;

    public int Priority => 0;

    public bool CanImport(ImportProbe probe)
    {
        ArgumentNullException.ThrowIfNull(probe);

        return probe.Extension is ".png" or ".jpg" or ".jpeg" or ".tga";
    }

    public async Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);

        var sourcePath = context.Input.SourcePath;
        EnsureSupportedExtension(sourcePath);

        var meta = await context.Files.GetMetadataAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        var sourceBytes = await context.Files.ReadAllBytesAsync(sourcePath, cancellationToken).ConfigureAwait(false);

        var virtualPath = DeriveVirtualPath(context.Input);
        var generatedSourcePath = virtualPath.TrimStart('/') + ".json";
        var intermediateCachePath = $"{AssetPipelineConstants.ImportedFolderName}/{sourcePath}";

        // Snapshot the payload deterministically.
        await context.Files.WriteAllBytesAsync(intermediateCachePath, sourceBytes, cancellationToken).ConfigureAwait(false);

        if (!TryIdentifyDimensions(context, sourcePath, sourceBytes.Span, out var width, out var height))
        {
            return [];
        }

        await CreateOrUpdateAuthoringSourceAsync(
            context,
            generatedSourcePath,
            sourcePath,
            width,
            height,
            cancellationToken).ConfigureAwait(false);

        return CreateImportedAsset(context, sourcePath, virtualPath, generatedSourcePath, intermediateCachePath, sourceBytes, meta.LastWriteTimeUtc);
    }

    private static void EnsureSupportedExtension(string sourcePath)
    {
        var ext = Path.GetExtension(sourcePath);
        if (string.Equals(ext, ".png", StringComparison.OrdinalIgnoreCase)
            || string.Equals(ext, ".jpg", StringComparison.OrdinalIgnoreCase)
            || string.Equals(ext, ".jpeg", StringComparison.OrdinalIgnoreCase)
            || string.Equals(ext, ".tga", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        throw new InvalidOperationException($"{nameof(ImageTextureImporter)} cannot import '{sourcePath}'.");
    }

    private static bool TryIdentifyDimensions(
        ImportContext context,
        string sourcePath,
        ReadOnlySpan<byte> bytes,
        out int width,
        out int height)
    {
        var info = Image.Identify(bytes);
        if (info is null)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_IMAGE_INVALID",
                message: $"Image file '{sourcePath}' is not a recognized image format.",
                sourcePath: sourcePath);

            if (context.Options.FailFast)
            {
                throw new InvalidDataException($"Unsupported image format '{sourcePath}'.");
            }

            width = 0;
            height = 0;
            return false;
        }

        width = info.Width;
        height = info.Height;
        return true;
    }

    private static IReadOnlyList<ImportedAsset> CreateImportedAsset(
        ImportContext context,
        string sourcePath,
        string virtualPath,
        string generatedSourcePath,
        string intermediateCachePath,
        ReadOnlyMemory<byte> sourceBytes,
        DateTimeOffset lastWriteTimeUtc)
    {
        var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Texture");
        var sourceHash = SHA256.HashData(sourceBytes.Span);
        var dependencies = DiscoverDependencies(sourcePath, generatedSourcePath);

        return [
            new ImportedAsset(
                AssetKey: assetKey,
                VirtualPath: virtualPath,
                AssetType: "Texture",
                Source: new ImportedAssetSource(
                    SourcePath: sourcePath,
                    SourceHashSha256: sourceHash,
                    LastWriteTimeUtc: lastWriteTimeUtc),
                Dependencies: dependencies,
                GeneratedSourcePath: generatedSourcePath,
                IntermediateCachePath: intermediateCachePath,
                Payload: null),
        ];
    }

    private static async Task CreateOrUpdateAuthoringSourceAsync(
        ImportContext context,
        string generatedSourcePath,
        string sourceImagePath,
        int width,
        int height,
        CancellationToken cancellationToken)
    {
        TextureSourceData? existing = null;
        try
        {
            var existingBytes = await context.Files.ReadAllBytesAsync(generatedSourcePath, cancellationToken).ConfigureAwait(false);
            existing = TextureSourceReader.Read(existingBytes.Span);
        }
        catch (FileNotFoundException)
        {
            // First import.
        }
        catch (Exception ex) when (ex is InvalidDataException or FormatException)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_TEXTURE_JSON_INVALID",
                message: ex.Message,
                sourcePath: generatedSourcePath);

            if (context.Options.FailFast)
            {
                throw;
            }

            existing = null;
        }

        // Defaults. Tool-owned fields: Imported.{Width,Height}
        var colorSpace = context.Input.Settings is not null && context.Input.Settings.TryGetValue("ColorSpace", out var cs)
            ? cs
            : "Srgb";

        var data = existing is null
            ? new TextureSourceData(
                Schema: "oxygen.texture.v1",
                Name: Path.GetFileNameWithoutExtension(sourceImagePath),
                SourceImage: sourceImagePath,
                ColorSpace: colorSpace,
                TextureType: "Texture2D",
                MipPolicy: new TextureMipPolicyData(Mode: "None"),
                RuntimeFormat: new TextureRuntimeFormatData(Format: "R8G8B8A8_UNorm", Compression: "None"),
                Imported: new TextureImportedData(width, height))
            : existing with
            {
                Imported = new TextureImportedData(width, height),
            };

        using var ms = new MemoryStream();
        TextureSourceWriter.Write(ms, data);
        await context.Files.WriteAllBytesAsync(generatedSourcePath, ms.ToArray(), cancellationToken).ConfigureAwait(false);
    }

    private static string DeriveVirtualPath(ImportInput input)
    {
        if (!string.IsNullOrWhiteSpace(input.VirtualPath))
        {
            if (!VirtualPath.IsCanonicalAbsolute(input.VirtualPath))
            {
                throw new InvalidDataException($"ImportInput.VirtualPath must be a canonical absolute virtual path: '{input.VirtualPath}'.");
            }

            if (!input.VirtualPath.EndsWith(AssetPipelineConstants.TextureExtension, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"ImportInput.VirtualPath must end with '{AssetPipelineConstants.TextureExtension}'.");
            }

            return input.VirtualPath;
        }

        var sourcePath = input.SourcePath;
        var otexRel = Path.ChangeExtension(sourcePath, AssetPipelineConstants.TextureExtension);
        var derived = "/" + otexRel;
        if (!VirtualPath.IsCanonicalAbsolute(derived))
        {
            throw new InvalidDataException($"Derived virtual path is not canonical: '{derived}'.");
        }

        return derived;
    }

    private static List<ImportedDependency> DiscoverDependencies(string sourceImagePath, string generatedSourcePath)
    {
        var seen = new HashSet<(ImportedDependencyKind, string)>();
        var deps = new List<ImportedDependency>(capacity: 4);

        Add(ImportedDependencyKind.SourceFile, sourceImagePath);
        Add(ImportedDependencyKind.SourceFile, generatedSourcePath);
        Add(ImportedDependencyKind.Sidecar, sourceImagePath + ".import.json");

        deps.Sort(static (a, b) =>
        {
            var kind = a.Kind.CompareTo(b.Kind);
            return kind != 0 ? kind : string.CompareOrdinal(a.Path, b.Path);
        });

        return deps;

        void Add(ImportedDependencyKind kind, string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return;
            }

            if (seen.Add((kind, path)))
            {
                deps.Add(new ImportedDependency(path, kind));
            }
        }
    }
}
