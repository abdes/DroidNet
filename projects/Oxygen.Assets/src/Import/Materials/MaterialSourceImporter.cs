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

        return [CreateImportedAsset(context, sourcePath, virtualPath, jsonBytes, material)];
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
        MaterialSource material)
    {
        var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Material");
        var sourceHash = SHA256.HashData(jsonBytes.Span);

        return new ImportedAsset(
            AssetKey: assetKey,
            VirtualPath: virtualPath,
            AssetType: "Material",
            Source: new ImportedAssetSource(
                SourcePath: sourcePath,
                SourceHashSha256: sourceHash,
                LastWriteTimeUtc: DateTimeOffset.UnixEpoch),
            Dependencies: [new ImportedDependency(sourcePath, ImportedDependencyKind.SourceFile)],
            Payload: material);
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
