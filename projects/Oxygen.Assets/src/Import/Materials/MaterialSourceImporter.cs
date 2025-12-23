// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Assets.Cook;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Imports authoring material source JSON (<c>*.omat.json</c>) and writes a cooked runtime <c>.omat</c> output.
/// </summary>
public sealed class MaterialSourceImporter : IAssetImporter
{
    private const string ImporterName = "Oxygen.Import.MaterialSource";
    private const string MaterialSourceSuffix = ".omat.json";

    /// <inheritdoc />
    public string Name => ImporterName;

    /// <inheritdoc />
    public int Priority => 0;

    /// <inheritdoc />
    public bool CanImport(ImportProbe probe)
    {
        ArgumentNullException.ThrowIfNull(probe);
        return probe.SourcePath.EndsWith(MaterialSourceSuffix, StringComparison.OrdinalIgnoreCase);
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);

        var sourcePath = context.Input.SourcePath;
        if (!sourcePath.EndsWith(MaterialSourceSuffix, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"{nameof(MaterialSourceImporter)} cannot import '{sourcePath}'.");
        }

        var meta = await context.Files.GetMetadataAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        var jsonBytes = await ReadJsonAsync(context.Files, sourcePath, cancellationToken).ConfigureAwait(false);
        if (!TryReadMaterial(jsonBytes.Span, context, out var material))
        {
            return [];
        }

        var virtualPath = DeriveVirtualPath(context.Input);
        var cookedRelativePath = ".cooked" + virtualPath;
        var cookedBytes = CookToBytes(material);

        await context.Files
            .WriteAllBytesAsync(cookedRelativePath, cookedBytes, cancellationToken)
            .ConfigureAwait(false);

        return [CreateImportedAsset(context, sourcePath, virtualPath, jsonBytes, meta.LastWriteTimeUtc, material)];
    }

    private static async ValueTask<ReadOnlyMemory<byte>> ReadJsonAsync(
        IImportFileAccess files,
        string sourcePath,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        return await files.ReadAllBytesAsync(sourcePath, cancellationToken).ConfigureAwait(false);
    }

    private static bool TryReadMaterial(ReadOnlySpan<byte> jsonUtf8, ImportContext context, out MaterialSource material)
    {
        try
        {
            material = MaterialSourceReader.Read(jsonUtf8);
            return true;
        }
        catch (Exception ex) when (ex is InvalidDataException or FormatException)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_MATERIAL_JSON_INVALID",
                message: ex.Message,
                sourcePath: context.Input.SourcePath);

            if (context.Options.FailFast)
            {
                throw;
            }

            material = default!;
            return false;
        }
    }

    private static ReadOnlyMemory<byte> CookToBytes(MaterialSource material)
    {
        byte[] cooked;
        using (var ms = new MemoryStream())
        {
            CookedMaterialWriter.Write(ms, material);
            cooked = ms.ToArray();
        }

        return cooked;
    }

    private static ImportedAsset CreateImportedAsset(
        ImportContext context,
        string sourcePath,
        string virtualPath,
        ReadOnlyMemory<byte> jsonBytes,
        DateTimeOffset lastWriteTimeUtc,
        MaterialSource material)
    {
        var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Material");
        var sourceHash = SHA256.HashData(jsonBytes.Span);
        var dependencies = DiscoverDependencies(sourcePath, material);

        return new ImportedAsset(
            AssetKey: assetKey,
            VirtualPath: virtualPath,
            AssetType: "Material",
            Source: new ImportedAssetSource(
                SourcePath: sourcePath,
                SourceHashSha256: sourceHash,
                LastWriteTimeUtc: lastWriteTimeUtc),
            Dependencies: dependencies,
            Payload: material);
    }

    private static List<ImportedDependency> DiscoverDependencies(string sourcePath, MaterialSource material)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        ArgumentNullException.ThrowIfNull(material);

        // Deterministic ordering is important for incremental rebuild decisions.
        // We keep SourceFile then Sidecar then ReferencedResource (sorted by path).
        var seen = new HashSet<(ImportedDependencyKind, string)>();
        var deps = new List<ImportedDependency>(capacity: 8);

        Add(ImportedDependencyKind.SourceFile, sourcePath);
        Add(ImportedDependencyKind.Sidecar, sourcePath + ".import.json");

        AddAssetUri(material.PbrMetallicRoughness.BaseColorTexture?.Source);
        AddAssetUri(material.PbrMetallicRoughness.MetallicRoughnessTexture?.Source);
        AddAssetUri(material.NormalTexture?.Source);
        AddAssetUri(material.OcclusionTexture?.Source);

        deps.Sort(static (a, b) =>
        {
            var kind = a.Kind.CompareTo(b.Kind);
            return kind != 0
                ? kind
                : string.CompareOrdinal(a.Path, b.Path);
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

        void AddAssetUri(string? assetUri)
        {
            if (string.IsNullOrWhiteSpace(assetUri))
            {
                return;
            }

            if (TryGetProjectRelativePathFromAssetUri(assetUri, out var projectRelativePath))
            {
                Add(ImportedDependencyKind.ReferencedResource, projectRelativePath);
            }
        }
    }

    private static bool TryGetProjectRelativePathFromAssetUri(string assetUri, out string projectRelativePath)
    {
        const string prefix = "asset://";

        if (!assetUri.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            projectRelativePath = string.Empty;
            return false;
        }

        // Important: we parse manually instead of using <see cref="Uri"/> for the authority because
        // Uri normalizes the host to lowercase, but our mount point tokens are case-sensitive in practice.
        var rest = assetUri[prefix.Length..];
        var slash = rest.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            projectRelativePath = string.Empty;
            return false;
        }

        var mountPoint = rest[..slash];
        var relativePath = rest[(slash + 1)..];

        var delimiter = relativePath.IndexOfAny(['?', '#']);
        if (delimiter >= 0)
        {
            relativePath = relativePath[..delimiter];
        }

        if (string.IsNullOrWhiteSpace(mountPoint) || string.IsNullOrWhiteSpace(relativePath))
        {
            projectRelativePath = string.Empty;
            return false;
        }

        projectRelativePath = mountPoint + "/" + Uri.UnescapeDataString(relativePath);
        return true;
    }

    private static string DeriveVirtualPath(ImportInput input)
    {
        ArgumentNullException.ThrowIfNull(input);

        if (!string.IsNullOrWhiteSpace(input.VirtualPath))
        {
            return input.VirtualPath;
        }

        // Default mapping for authoring materials:
        // Content/Materials/Wood.omat.json -> /Content/Materials/Wood.omat
        var withoutJson = input.SourcePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? input.SourcePath[..^".json".Length]
            : input.SourcePath;

        return !string.IsNullOrEmpty(withoutJson) && withoutJson[0] == '/'
            ? withoutJson
            : "/" + withoutJson;
    }
}
