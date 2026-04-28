// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Default scalar material document service.
/// </summary>
public sealed class MaterialDocumentService : IMaterialDocumentService
{
    private const string MaterialSchema = "oxygen.material.v1";
    private const string MaterialType = "PBR";

    private readonly IMaterialSourcePathResolver pathResolver;
    private readonly IMaterialCookService cookService;
    private readonly IOperationResultPublisher? operationResults;
    private readonly Dictionary<Guid, MaterialDocument> documents = [];
    private readonly Lock sync = new();

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialDocumentService"/> class.
    /// </summary>
    /// <param name="pathResolver">The material source path resolver.</param>
    /// <param name="cookService">The material cook service.</param>
    /// <param name="operationResults">Optional operation-result publisher.</param>
    public MaterialDocumentService(
        IMaterialSourcePathResolver pathResolver,
        IMaterialCookService cookService,
        IOperationResultPublisher? operationResults = null)
    {
        this.pathResolver = pathResolver ?? throw new ArgumentNullException(nameof(pathResolver));
        this.cookService = cookService ?? throw new ArgumentNullException(nameof(cookService));
        this.operationResults = operationResults;
    }

    /// <inheritdoc />
    public async Task<MaterialDocument> CreateAsync(
        Uri targetUri,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(targetUri);
        cancellationToken.ThrowIfCancellationRequested();

        var location = this.pathResolver.Resolve(targetUri);
        if (File.Exists(location.SourcePath))
        {
            throw new IOException($"Material source '{location.SourcePath}' already exists.");
        }

        var assetName = GetMaterialDisplayName(location.MaterialUri);
        var source = CreateDefaultSource(assetName);
        await WriteSourceAsync(location.SourcePath, source, cancellationToken).ConfigureAwait(false);

        return this.Track(location, source, assetName, MaterialCookState.NotCooked, isDirty: false);
    }

    /// <inheritdoc />
    public async Task<MaterialDocument> OpenAsync(Uri sourceUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sourceUri);
        cancellationToken.ThrowIfCancellationRequested();

        var location = this.pathResolver.Resolve(sourceUri);
        var json = await File.ReadAllBytesAsync(location.SourcePath, cancellationToken).ConfigureAwait(false);
        var displayName = GetMaterialDisplayName(location.MaterialUri);
        var source = WithName(MaterialSourceReader.Read(json), displayName);

        return this.Track(
            location,
            source,
            displayName,
            MaterialCookState.NotCooked,
            isDirty: false);
    }

    /// <inheritdoc />
    public Task<MaterialEditResult> EditScalarAsync(
        Guid documentId,
        MaterialFieldEdit edit,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        MaterialDocument document;
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out document!))
            {
                throw new KeyNotFoundException($"Material document '{documentId}' is not open.");
            }
        }

        if (!TryApplyEdit(document.Source, edit, out var updatedSource))
        {
            var operationId = this.PublishMaterialFailure(
                MaterialOperationKinds.EditScalar,
                document,
                GetRejectedEditCode(edit),
                "Material field was not changed.",
                GetRejectedEditMessage(edit),
                FailureDomain.MaterialAuthoring);
            return Task.FromResult(new MaterialEditResult(Succeeded: false, OperationId: operationId));
        }

        lock (this.sync)
        {
            this.documents[documentId] = document with
            {
                Source = updatedSource,
                Asset = CreateAsset(document.MaterialUri, updatedSource),
                IsDirty = true,
                CookState = MaterialCookState.Stale,
            };
        }

        return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
    }

    /// <inheritdoc />
    public async Task<MaterialSaveResult> SaveAsync(Guid documentId, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        MaterialDocument document;
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out document!))
            {
                throw new KeyNotFoundException($"Material document '{documentId}' is not open.");
            }
        }

        var source = WithName(document.Source, document.DisplayName);
        await WriteSourceAsync(document.SourcePath, source, cancellationToken).ConfigureAwait(false);
        lock (this.sync)
        {
            if (this.documents.TryGetValue(documentId, out var current))
            {
                this.documents[documentId] = current with
                {
                    Source = source,
                    Asset = CreateAsset(current.MaterialUri, source),
                    IsDirty = false,
                };
            }
        }

        return new MaterialSaveResult(Succeeded: true, OperationId: null);
    }

    /// <inheritdoc />
    public async Task<MaterialCookResult> CookAsync(Guid documentId, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        MaterialDocument document;
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out document!))
            {
                throw new KeyNotFoundException($"Material document '{documentId}' is not open.");
            }
        }

        if (document.IsDirty)
        {
            var operationId = this.PublishMaterialFailure(
                MaterialOperationKinds.Cook,
                document,
                MaterialDiagnosticCodes.DescriptorDirty,
                "Material cook was rejected.",
                "Save the material descriptor before cooking it.",
                FailureDomain.ContentPipeline);
            var rejected = new MaterialCookResult(
                document.MaterialUri,
                CookedMaterialUri: null,
                MaterialCookState.Rejected,
                OperationId: operationId);
            lock (this.sync)
            {
                if (this.documents.TryGetValue(documentId, out var current))
                {
                    this.documents[documentId] = current with { CookState = rejected.State };
                }
            }

            return rejected;
        }

        var location = this.pathResolver.Resolve(document.MaterialUri);
        var result = await this.cookService.CookMaterialAsync(
            new MaterialCookRequest(
                MaterialSourceUri: document.MaterialUri,
                ProjectRoot: location.ProjectRoot,
                MountName: location.MountName,
                SourceRelativePath: location.SourceRelativePath),
            cancellationToken).ConfigureAwait(false);

        lock (this.sync)
        {
            if (this.documents.TryGetValue(documentId, out var current))
            {
                this.documents[documentId] = current with { CookState = result.State };
            }
        }

        return result;
    }

    /// <inheritdoc />
    public Task CloseAsync(Guid documentId, bool discard, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        MaterialDocument? document;
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out document))
            {
                return Task.CompletedTask;
            }
        }

        if (document.IsDirty && !discard)
        {
            throw new InvalidOperationException("Cannot close a dirty material document without discard.");
        }

        lock (this.sync)
        {
            _ = this.documents.Remove(documentId);
        }

        return Task.CompletedTask;
    }

    private static MaterialSource CreateDefaultSource(string displayName)
        => new(
            schema: MaterialSchema,
            type: MaterialType,
            name: displayName,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 1.0f,
                baseColorG: 1.0f,
                baseColorB: 1.0f,
                baseColorA: 1.0f,
                metallicFactor: 0.0f,
                roughnessFactor: 0.5f,
                baseColorTexture: null,
                metallicRoughnessTexture: null),
            normalTexture: null,
            occlusionTexture: null,
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);

    private static MaterialAsset CreateAsset(Uri materialUri, MaterialSource source)
        => new() { Uri = materialUri, Source = source };

    private static bool TryApplyEdit(
        MaterialSource source,
        MaterialFieldEdit edit,
        out MaterialSource updated)
    {
        updated = source;

        if (string.IsNullOrWhiteSpace(edit.FieldKey))
        {
            return false;
        }

        switch (edit.FieldKey)
        {
            case MaterialFieldKeys.Name:
                return false;

            case MaterialFieldKeys.AlphaMode:
                if (!TryGetAlphaMode(edit.NewValue, out var alphaMode))
                {
                    return false;
                }

                updated = WithAlphaMode(source, alphaMode);
                return true;

            case MaterialFieldKeys.DoubleSided:
                if (edit.NewValue is not bool doubleSided)
                {
                    return false;
                }

                updated = WithDoubleSided(source, doubleSided);
                return true;
        }

        if (!TryGetFiniteFloat(edit.NewValue, out var value))
        {
            return false;
        }

        return TryApplyFloatEdit(source, edit.FieldKey, value, out updated);
    }

    private static bool TryApplyFloatEdit(
        MaterialSource source,
        string fieldKey,
        float value,
        out MaterialSource updated)
    {
        updated = source;

        var clamped01 = Math.Clamp(value, 0.0f, 1.0f);

        var pbr = source.PbrMetallicRoughness;
        if (TryUpdatePbr(pbr, fieldKey, clamped01, out var updatedPbr))
        {
            updated = WithPbr(source, updatedPbr);
            return true;
        }

        if (fieldKey == MaterialFieldKeys.AlphaCutoff)
        {
            updated = WithAlphaCutoff(source, clamped01);
            return true;
        }

        if (fieldKey == MaterialFieldKeys.NormalTextureScale && source.NormalTexture is { } normal)
        {
            updated = WithNormalTexture(source, normal with { Scale = Math.Max(0.0f, value) });
            return true;
        }

        if (fieldKey == MaterialFieldKeys.OcclusionTextureStrength && source.OcclusionTexture is { } occlusion)
        {
            updated = WithOcclusionTexture(source, occlusion with { Strength = clamped01 });
            return true;
        }

        return false;
    }

    private static bool TryUpdatePbr(
        MaterialPbrMetallicRoughness pbr,
        string fieldKey,
        float value,
        out MaterialPbrMetallicRoughness updated)
    {
        updated = fieldKey switch
        {
            MaterialFieldKeys.BaseColorR => CreatePbr(pbr, baseColorR: value),
            MaterialFieldKeys.BaseColorG => CreatePbr(pbr, baseColorG: value),
            MaterialFieldKeys.BaseColorB => CreatePbr(pbr, baseColorB: value),
            MaterialFieldKeys.BaseColorA => CreatePbr(pbr, baseColorA: value),
            MaterialFieldKeys.MetallicFactor => CreatePbr(pbr, metallicFactor: value),
            MaterialFieldKeys.RoughnessFactor => CreatePbr(pbr, roughnessFactor: value),
            _ => null!,
        };

        return updated is not null;
    }

    private static MaterialPbrMetallicRoughness CreatePbr(
        MaterialPbrMetallicRoughness source,
        float? baseColorR = null,
        float? baseColorG = null,
        float? baseColorB = null,
        float? baseColorA = null,
        float? metallicFactor = null,
        float? roughnessFactor = null)
        => new(
            baseColorR: baseColorR ?? source.BaseColorR,
            baseColorG: baseColorG ?? source.BaseColorG,
            baseColorB: baseColorB ?? source.BaseColorB,
            baseColorA: baseColorA ?? source.BaseColorA,
            metallicFactor: metallicFactor ?? source.MetallicFactor,
            roughnessFactor: roughnessFactor ?? source.RoughnessFactor,
            baseColorTexture: source.BaseColorTexture,
            metallicRoughnessTexture: source.MetallicRoughnessTexture);

    private static bool TryGetAlphaMode(object? value, out MaterialAlphaMode alphaMode)
    {
        switch (value)
        {
            case MaterialAlphaMode mode:
                alphaMode = mode;
                return true;
            case string text:
                return TryParseAlphaMode(text, out alphaMode);
            default:
                alphaMode = MaterialAlphaMode.Opaque;
                return false;
        }
    }

    private static bool TryParseAlphaMode(string text, out MaterialAlphaMode alphaMode)
    {
        if (Enum.TryParse(text, ignoreCase: true, out alphaMode))
        {
            return true;
        }

        alphaMode = text.Trim().ToUpperInvariant() switch
        {
            "OPAQUE" => MaterialAlphaMode.Opaque,
            "MASK" => MaterialAlphaMode.Mask,
            "BLEND" => MaterialAlphaMode.Blend,
            _ => alphaMode,
        };

        return text.Trim().Equals("OPAQUE", StringComparison.OrdinalIgnoreCase)
            || text.Trim().Equals("MASK", StringComparison.OrdinalIgnoreCase)
            || text.Trim().Equals("BLEND", StringComparison.OrdinalIgnoreCase);
    }

    private static bool TryGetFiniteFloat(object? value, out float number)
    {
        number = value switch
        {
            float f => f,
            double d => (float)d,
            decimal m => (float)m,
            int i => i,
            long l => l,
            string text when float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed) => parsed,
            _ => float.NaN,
        };

        return float.IsFinite(number);
    }

    private static async Task WriteSourceAsync(
        string sourcePath,
        MaterialSource source,
        CancellationToken cancellationToken)
    {
        var directory = Path.GetDirectoryName(sourcePath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        using var memory = new MemoryStream();
        MaterialSourceWriter.Write(memory, source);
        await File.WriteAllBytesAsync(sourcePath, memory.ToArray(), cancellationToken).ConfigureAwait(false);
    }

    private static MaterialSource WithName(MaterialSource source, string? name)
        => new(
            source.Schema,
            source.Type,
            name,
            source.PbrMetallicRoughness,
            source.NormalTexture,
            source.OcclusionTexture,
            source.AlphaMode,
            source.AlphaCutoff,
            source.DoubleSided);

    private static MaterialSource WithPbr(MaterialSource source, MaterialPbrMetallicRoughness pbr)
        => new(
            source.Schema,
            source.Type,
            source.Name,
            pbr,
            source.NormalTexture,
            source.OcclusionTexture,
            source.AlphaMode,
            source.AlphaCutoff,
            source.DoubleSided);

    private static MaterialSource WithNormalTexture(MaterialSource source, NormalTextureRef normal)
        => new(
            source.Schema,
            source.Type,
            source.Name,
            source.PbrMetallicRoughness,
            normal,
            source.OcclusionTexture,
            source.AlphaMode,
            source.AlphaCutoff,
            source.DoubleSided);

    private static MaterialSource WithOcclusionTexture(MaterialSource source, OcclusionTextureRef occlusion)
        => new(
            source.Schema,
            source.Type,
            source.Name,
            source.PbrMetallicRoughness,
            source.NormalTexture,
            occlusion,
            source.AlphaMode,
            source.AlphaCutoff,
            source.DoubleSided);

    private static MaterialSource WithAlphaMode(MaterialSource source, MaterialAlphaMode alphaMode)
        => new(
            source.Schema,
            source.Type,
            source.Name,
            source.PbrMetallicRoughness,
            source.NormalTexture,
            source.OcclusionTexture,
            alphaMode,
            source.AlphaCutoff,
            source.DoubleSided);

    private static MaterialSource WithAlphaCutoff(MaterialSource source, float alphaCutoff)
        => new(
            source.Schema,
            source.Type,
            source.Name,
            source.PbrMetallicRoughness,
            source.NormalTexture,
            source.OcclusionTexture,
            source.AlphaMode,
            alphaCutoff,
            source.DoubleSided);

    private static MaterialSource WithDoubleSided(MaterialSource source, bool doubleSided)
        => new(
            source.Schema,
            source.Type,
            source.Name,
            source.PbrMetallicRoughness,
            source.NormalTexture,
            source.OcclusionTexture,
            source.AlphaMode,
            source.AlphaCutoff,
            doubleSided);

    private static string GetMaterialDisplayName(Uri materialUri)
    {
        var fileName = Uri.UnescapeDataString(Path.GetFileName(materialUri.AbsolutePath));
        var displayName = StripKnownMaterialExtension(fileName);
        if (string.IsNullOrWhiteSpace(displayName))
        {
            throw new InvalidOperationException($"Material URI '{materialUri}' does not contain a material file name.");
        }

        return displayName;
    }

    private static string StripKnownMaterialExtension(string fileName)
    {
        const string SourceExtension = ".omat.json";
        const string CookedExtension = ".omat";

        if (fileName.EndsWith(SourceExtension, StringComparison.OrdinalIgnoreCase))
        {
            return fileName[..^SourceExtension.Length];
        }

        if (fileName.EndsWith(CookedExtension, StringComparison.OrdinalIgnoreCase))
        {
            return fileName[..^CookedExtension.Length];
        }

        return Path.GetFileNameWithoutExtension(fileName);
    }

    private MaterialDocument Track(
        MaterialSourceLocation location,
        MaterialSource source,
        string displayName,
        MaterialCookState cookState,
        bool isDirty)
    {
        var document = new MaterialDocument(
            DocumentId: Guid.NewGuid(),
            MaterialUri: location.MaterialUri,
            MaterialGuid: CreateMaterialGuid(location.MaterialUri),
            SourcePath: location.SourcePath,
            DisplayName: displayName,
            Source: source,
            Asset: CreateAsset(location.MaterialUri, source),
            IsDirty: isDirty,
            CookState: cookState);

        lock (this.sync)
        {
            this.documents[document.DocumentId] = document;
        }

        return document;
    }

    private Guid? PublishMaterialFailure(
        string operationKind,
        MaterialDocument document,
        string code,
        string title,
        string message,
        FailureDomain domain)
    {
        if (this.operationResults is null)
        {
            return null;
        }

        var operationId = Guid.NewGuid();
        var scope = new AffectedScope
        {
            DocumentId = document.DocumentId,
            DocumentPath = document.SourcePath,
            DocumentName = document.DisplayName,
            AssetId = document.MaterialUri.ToString(),
            AssetSourcePath = document.SourcePath,
            AssetVirtualPath = document.MaterialUri.AbsolutePath,
        };
        var diagnostic = new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = domain,
            Severity = DiagnosticSeverity.Error,
            Code = code,
            Message = message,
            AffectedPath = document.SourcePath,
            AffectedVirtualPath = document.MaterialUri.AbsolutePath,
            AffectedEntity = scope,
        };

        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = scope,
            Diagnostics = [diagnostic],
        });

        return operationId;
    }

    private static string GetRejectedEditCode(MaterialFieldEdit edit)
    {
        _ = edit;
        return MaterialDiagnosticCodes.FieldRejected;
    }

    private static string GetRejectedEditMessage(MaterialFieldEdit edit)
        => string.Equals(edit.FieldKey, MaterialFieldKeys.Name, StringComparison.Ordinal)
            ? "Material names are controlled by the asset file name."
            : $"Material field '{edit.FieldKey}' rejected value '{edit.NewValue}'.";

    private static Guid CreateMaterialGuid(Uri materialUri)
    {
        var text = materialUri.AbsoluteUri.ToUpperInvariant();
        Span<byte> hash = stackalloc byte[32];
        _ = SHA256.HashData(Encoding.UTF8.GetBytes(text), hash);

        Span<byte> bytes = stackalloc byte[16];
        hash[..16].CopyTo(bytes);

        bytes[7] = (byte)((bytes[7] & 0x0F) | 0x50);
        bytes[8] = (byte)((bytes[8] & 0x3F) | 0x80);
        return new Guid(bytes);
    }
}
