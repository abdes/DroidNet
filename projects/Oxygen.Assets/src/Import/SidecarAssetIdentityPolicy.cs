// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Import;

/// <summary>
/// Sidecar-backed identity policy that persists <see cref="AssetKey"/> ownership next to the source file.
/// </summary>
/// <remarks>
/// The sidecar file name is <c>&lt;SourcePath&gt;.import.json</c>.
/// This policy is source-scoped (one instance per import input).
/// </remarks>
public sealed partial class SidecarAssetIdentityPolicy : IAssetIdentityPolicy
{
    private const int SchemaVersion = 1;

    private readonly IImportFileAccess files;
    private readonly ImportInput input;
    private readonly SidecarImporterData importer;
    private SidecarData? cached;

    /// <summary>
    /// Initializes a new instance of the <see cref="SidecarAssetIdentityPolicy"/> class.
    /// </summary>
    /// <param name="files">File access abstraction.</param>
    /// <param name="input">The source import input.</param>
    public SidecarAssetIdentityPolicy(IImportFileAccess files, ImportInput input)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(input);

        this.files = files;
        this.input = input;
        this.importer = new SidecarImporterData(
            Name: "(unknown)",
            Type: null,
            Version: "0",
            Settings: new Dictionary<string, JsonElement>(StringComparer.Ordinal));
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SidecarAssetIdentityPolicy"/> class.
    /// </summary>
    /// <param name="files">File access abstraction.</param>
    /// <param name="input">The source import input.</param>
    /// <param name="importer">The selected importer.</param>
    /// <param name="options">Import options.</param>
    public SidecarAssetIdentityPolicy(IImportFileAccess files, ImportInput input, IAssetImporter importer, ImportOptions options)
        : this(files, input)
    {
        ArgumentNullException.ThrowIfNull(importer);
        ArgumentNullException.ThrowIfNull(options);

        this.importer = CreateImporterData(importer, options, input);
    }

    /// <inheritdoc />
    public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(virtualPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(assetType);

        // This policy is synchronous by interface contract.
        this.cached ??= this.LoadOrCreateSidecarAsync(CancellationToken.None).GetAwaiter().GetResult();

        if (TryUpdateImporterMetadata(this.cached, this.importer, out var updated))
        {
            this.cached = updated;
            this.SaveSidecarAsync(this.cached, CancellationToken.None).GetAwaiter().GetResult();
        }

        var outputs = this.cached.Outputs;
        var existing = outputs.FirstOrDefault(o => string.Equals(o.VirtualPath, virtualPath, StringComparison.Ordinal));
        if (existing is not null && AssetKeyString.TryParse(existing.AssetKey, out var parsed))
        {
            return parsed;
        }

        var created = AssetKeyString.CreateRandom();

        outputs.Add(
            new SidecarOutputData(
                Role: "Primary",
                AssetType: assetType,
                AssetKey: created.ToString(),
                VirtualPath: virtualPath));

        this.SaveSidecarAsync(this.cached, CancellationToken.None).GetAwaiter().GetResult();
        return created;
    }

    internal async ValueTask<IReadOnlyList<ImportedAsset>?> TryGetUpToDateImportedAssetsAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var data = await this.TryLoadSidecarAsync(cancellationToken).ConfigureAwait(false);
        if (data is null)
        {
            return null;
        }

        if (!ImporterEquals(data.Importer, this.importer))
        {
            return null;
        }

        if (data.Incremental is null || data.Outputs.Count == 0)
        {
            return null;
        }

        if (!await this.IsSourceUpToDateAsync(data, cancellationToken).ConfigureAwait(false))
        {
            return null;
        }

        if (!await this.AreDependenciesUpToDateAsync(data.Incremental.Dependencies, cancellationToken).ConfigureAwait(false))
        {
            return null;
        }

        var sourceHash = Convert.FromHexString(data.Source.Sha256);

        var deps = data.Incremental.Dependencies
            .ConvertAll(static d => new ImportedDependency(d.RelativePath, d.Kind))
;

        var assets = new List<ImportedAsset>(data.Outputs.Count);
        foreach (var output in data.Outputs)
        {
            if (!AssetKeyString.TryParse(output.AssetKey, out var key))
            {
                return null;
            }

            assets.Add(
                new ImportedAsset(
                    AssetKey: key,
                    VirtualPath: output.VirtualPath,
                    AssetType: output.AssetType,
                    Source: new ImportedAssetSource(
                        SourcePath: this.input.SourcePath,
                        SourceHashSha256: sourceHash,
                        LastWriteTimeUtc: data.Source.LastWriteTimeUtc),
                    Dependencies: deps,
                    Payload: "(up-to-date)"));
        }

        return assets;
    }

    internal async ValueTask RecordImportAsync(
        IReadOnlyList<ImportedAsset> assets,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assets);
        cancellationToken.ThrowIfCancellationRequested();

        // Only record incrementality for successful imports that produced outputs.
        if (assets.Count == 0)
        {
            return;
        }

        await this.EnsureSidecarLoadedAndUpToDateImporterAsync(cancellationToken).ConfigureAwait(false);

        var sourceFingerprint = await ComputeFingerprintAsync(this.files, this.input.SourcePath, cancellationToken).ConfigureAwait(false);
        var deps = CollectDependencies(assets);
        var depFingerprints = await this.ComputeDependencyFingerprintsAsync(deps, cancellationToken).ConfigureAwait(false);

        this.cached = this.UpdateSidecarFromImport(sourceFingerprint, depFingerprints);
        await this.SaveSidecarAsync(this.cached, cancellationToken).ConfigureAwait(false);
    }

    private static SidecarDependencyData CreateMissingDependencyFingerprint(ImportedDependency dep)
        => new(
            RelativePath: dep.Path,
            Kind: dep.Kind,
            LastWriteTimeUtc: DateTimeOffset.UnixEpoch,
            Sha256: null,
            ByteLength: null);

    private static async Task<SidecarData> CreateNewSidecarAsync(IImportFileAccess files, ImportInput input, CancellationToken cancellationToken)
    {
        var fp = await ComputeFingerprintAsync(files, input.SourcePath, cancellationToken).ConfigureAwait(false);

        return new SidecarData(
            SchemaVersion: SchemaVersion,
            MountPoint: input.MountPoint,
            Source: new SidecarSourceData(
                RelativePath: input.SourcePath,
                LastWriteTimeUtc: fp.LastWriteTimeUtc,
                Sha256: fp.Sha256Hex,
                ByteLength: fp.ByteLength),
            Importer: new SidecarImporterData(
                Name: "(unknown)",
                Type: null,
                Version: "0",
                Settings: new Dictionary<string, JsonElement>(StringComparer.Ordinal)),
            Outputs: [],
            Incremental: null);
    }

    private static string GetSidecarPath(string sourcePath) => sourcePath + ".import.json";

    private static SidecarImporterData CreateImporterData(IAssetImporter importer, ImportOptions options, ImportInput input)
    {
        var version = importer.GetType().Assembly.GetName().Version?.ToString() ?? "0";

        var settings = new Dictionary<string, JsonElement>(StringComparer.Ordinal)
        {
            ["FailFast"] = JsonSerializer.SerializeToElement(options.FailFast),
            ["ReimportIfUnchanged"] = JsonSerializer.SerializeToElement(options.ReimportIfUnchanged),
            ["LogLevel"] = JsonSerializer.SerializeToElement(options.LogLevel.ToString()),
        };

        if (input.Settings is not null)
        {
            foreach (var (key, value) in input.Settings)
            {
                settings[key] = JsonSerializer.SerializeToElement(value);
            }
        }

        return new SidecarImporterData(
            Name: importer.Name,
            Type: importer.GetType().FullName,
            Version: version,
            Settings: settings);
    }

    private static bool TryUpdateImporterMetadata(SidecarData existing, SidecarImporterData desired, out SidecarData updated)
    {
        ArgumentNullException.ThrowIfNull(existing);
        ArgumentNullException.ThrowIfNull(desired);

        if (string.Equals(existing.Importer.Name, desired.Name, StringComparison.Ordinal)
            && string.Equals(existing.Importer.Type, desired.Type, StringComparison.Ordinal)
            && string.Equals(existing.Importer.Version, desired.Version, StringComparison.Ordinal)
            && DictionaryEquals(existing.Importer.Settings, desired.Settings))
        {
            updated = existing;
            return false;
        }

        updated = existing with { Importer = desired };
        return true;
    }

    private static bool DictionaryEquals(Dictionary<string, JsonElement> a, Dictionary<string, JsonElement> b)
    {
        if (ReferenceEquals(a, b))
        {
            return true;
        }

        if (a.Count != b.Count)
        {
            return false;
        }

        foreach (var pair in a)
        {
            if (!b.TryGetValue(pair.Key, out var other))
            {
                return false;
            }

            if (!JsonElement.DeepEquals(pair.Value, other))
            {
                return false;
            }
        }

        return true;
    }

    private static bool ImporterEquals(SidecarImporterData a, SidecarImporterData b)
        => string.Equals(a.Name, b.Name, StringComparison.Ordinal)
            && string.Equals(a.Type, b.Type, StringComparison.Ordinal)
            && string.Equals(a.Version, b.Version, StringComparison.Ordinal)
            && DictionaryEquals(a.Settings, b.Settings);

    private static List<ImportedDependency> CollectDependencies(IReadOnlyList<ImportedAsset> assets)
    {
        var seen = new HashSet<(ImportedDependencyKind kind, string path)>();
        var list = new List<ImportedDependency>();

        foreach (var asset in assets)
        {
            foreach (var dep in asset.Dependencies)
            {
                if (string.IsNullOrWhiteSpace(dep.Path))
                {
                    continue;
                }

                if (seen.Add((dep.Kind, dep.Path)))
                {
                    list.Add(dep);
                }
            }
        }

        list.Sort(static (a, b) =>
        {
            var kind = a.Kind.CompareTo(b.Kind);
            return kind != 0
                ? kind
                : string.CompareOrdinal(a.Path, b.Path);
        });

        return list;
    }

    private static async ValueTask<FileFingerprint> ComputeFingerprintAsync(
        IImportFileAccess files,
        string sourcePath,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);

        var meta = await files.GetMetadataAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        var bytes = await files.ReadAllBytesAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        var sha256 = SHA256.HashData(bytes.Span);

        return new FileFingerprint(
            ByteLength: meta.ByteLength,
            LastWriteTimeUtc: meta.LastWriteTimeUtc,
            Sha256Hex: Convert.ToHexString(sha256));
    }

    private async ValueTask EnsureSidecarLoadedAndUpToDateImporterAsync(CancellationToken cancellationToken)
    {
        this.cached ??= await this.LoadOrCreateSidecarAsync(cancellationToken).ConfigureAwait(false);

        if (TryUpdateImporterMetadata(this.cached, this.importer, out var updatedImporter))
        {
            this.cached = updatedImporter;
        }
    }

    private async ValueTask<List<SidecarDependencyData>> ComputeDependencyFingerprintsAsync(
        List<ImportedDependency> deps,
        CancellationToken cancellationToken)
    {
        var depFingerprints = new List<SidecarDependencyData>(deps.Count);
        foreach (var dep in deps)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (dep.Kind == ImportedDependencyKind.Sidecar)
            {
                depFingerprints.Add(CreateMissingDependencyFingerprint(dep));
                continue;
            }

            depFingerprints.Add(await this.TryComputeDependencyFingerprintAsync(dep, cancellationToken).ConfigureAwait(false));
        }

        return depFingerprints;
    }

    private async ValueTask<SidecarDependencyData> TryComputeDependencyFingerprintAsync(
        ImportedDependency dep,
        CancellationToken cancellationToken)
    {
        try
        {
            var fp = await ComputeFingerprintAsync(this.files, dep.Path, cancellationToken).ConfigureAwait(false);
            return new SidecarDependencyData(
                RelativePath: dep.Path,
                Kind: dep.Kind,
                LastWriteTimeUtc: fp.LastWriteTimeUtc,
                Sha256: fp.Sha256Hex,
                ByteLength: fp.ByteLength);
        }
        catch (FileNotFoundException)
        {
            return CreateMissingDependencyFingerprint(dep);
        }
    }

    private SidecarData UpdateSidecarFromImport(
        FileFingerprint sourceFingerprint,
        List<SidecarDependencyData> depFingerprints)
        => this.cached! with
        {
            Source = new SidecarSourceData(
                RelativePath: this.input.SourcePath,
                LastWriteTimeUtc: sourceFingerprint.LastWriteTimeUtc,
                Sha256: sourceFingerprint.Sha256Hex,
                ByteLength: sourceFingerprint.ByteLength),
            Incremental = new SidecarIncrementalData(
                LastImportTimeUtc: DateTimeOffset.UtcNow,
                Dependencies: depFingerprints),
        };

    private async ValueTask<bool> IsSourceUpToDateAsync(SidecarData data, CancellationToken cancellationToken)
    {
        try
        {
            var fp = await ComputeFingerprintAsync(this.files, this.input.SourcePath, cancellationToken).ConfigureAwait(false);
            return fp.ByteLength == data.Source.ByteLength
                && string.Equals(fp.Sha256Hex, data.Source.Sha256, StringComparison.Ordinal)
                && fp.LastWriteTimeUtc == data.Source.LastWriteTimeUtc;
        }
        catch (FileNotFoundException)
        {
            return false;
        }
    }

    private async ValueTask<bool> AreDependenciesUpToDateAsync(
        IReadOnlyList<SidecarDependencyData> deps,
        CancellationToken cancellationToken)
    {
        foreach (var dep in deps)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (dep.Kind == ImportedDependencyKind.Sidecar)
            {
                continue;
            }

            if (string.IsNullOrWhiteSpace(dep.Sha256) || dep.ByteLength is null)
            {
                return false;
            }

            try
            {
                var fp = await ComputeFingerprintAsync(this.files, dep.RelativePath, cancellationToken).ConfigureAwait(false);
                if (fp.ByteLength != dep.ByteLength.Value
                    || !string.Equals(fp.Sha256Hex, dep.Sha256, StringComparison.Ordinal)
                    || fp.LastWriteTimeUtc != dep.LastWriteTimeUtc)
                {
                    return false;
                }
            }
            catch (FileNotFoundException)
            {
                return false;
            }
        }

        return true;
    }

    private async ValueTask<SidecarData?> TryLoadSidecarAsync(CancellationToken cancellationToken)
    {
        var sidecarPath = GetSidecarPath(this.input.SourcePath);
        try
        {
            var bytes = await this.files.ReadAllBytesAsync(sidecarPath, cancellationToken).ConfigureAwait(false);
            return JsonSerializer.Deserialize(bytes.Span, SidecarSerializationContext.Default.SidecarData);
        }
        catch (FileNotFoundException)
        {
            return null;
        }
        catch (InvalidDataException)
        {
            return null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private async Task<SidecarData> LoadOrCreateSidecarAsync(CancellationToken cancellationToken)
    {
        var sidecarPath = GetSidecarPath(this.input.SourcePath);

        try
        {
            var bytes = await this.files.ReadAllBytesAsync(sidecarPath, cancellationToken).ConfigureAwait(false);
            var data = JsonSerializer.Deserialize(bytes.Span, SidecarSerializationContext.Default.SidecarData)
                ?? throw new InvalidDataException("Sidecar JSON is empty or invalid.");

            return data;
        }
        catch (FileNotFoundException)
        {
            var initial = await CreateNewSidecarAsync(this.files, this.input, cancellationToken).ConfigureAwait(false);
            await this.SaveSidecarAsync(initial, cancellationToken).ConfigureAwait(false);
            return initial;
        }
    }

    private async Task SaveSidecarAsync(SidecarData data, CancellationToken cancellationToken)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(data, SidecarSerializationContext.Default.SidecarData);
        await this.files.WriteAllBytesAsync(GetSidecarPath(this.input.SourcePath), bytes, cancellationToken).ConfigureAwait(false);
    }

    internal sealed record SidecarData(
        int SchemaVersion,
        string MountPoint,
        SidecarSourceData Source,
        SidecarImporterData Importer,
        List<SidecarOutputData> Outputs,
        SidecarIncrementalData? Incremental = null);

    internal sealed record SidecarSourceData(
        string RelativePath,
        DateTimeOffset LastWriteTimeUtc,
        string Sha256,
        long ByteLength);

    internal sealed record SidecarImporterData(
        string Name,
        string? Type,
        string Version,
        Dictionary<string, JsonElement> Settings);

    internal sealed record SidecarOutputData(
        string Role,
        string AssetType,
        string AssetKey,
        string VirtualPath);

    internal sealed record SidecarIncrementalData(
        DateTimeOffset LastImportTimeUtc,
        List<SidecarDependencyData> Dependencies);

    internal sealed record SidecarDependencyData(
        string RelativePath,
        ImportedDependencyKind Kind,
        DateTimeOffset LastWriteTimeUtc,
        string? Sha256,
        long? ByteLength);

    private sealed record FileFingerprint(
        long ByteLength,
        DateTimeOffset LastWriteTimeUtc,
        string Sha256Hex);

    [JsonSourceGenerationOptions(
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
        AllowTrailingCommas = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow)]
    [JsonSerializable(typeof(SidecarData))]
    internal sealed partial class SidecarSerializationContext : JsonSerializerContext;

    private static class AssetKeyString
    {
        public static bool TryParse(string? value, out AssetKey key)
        {
            key = default;
            if (string.IsNullOrWhiteSpace(value) || value.Length != 32)
            {
                return false;
            }

            if (!ulong.TryParse(value.AsSpan(0, 16), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var p0))
            {
                return false;
            }

            if (!ulong.TryParse(value.AsSpan(16, 16), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var p1))
            {
                return false;
            }

            key = new AssetKey(p0, p1);
            return true;
        }

        public static AssetKey CreateRandom()
        {
            Span<byte> bytes = stackalloc byte[16];
            RandomNumberGenerator.Fill(bytes);
            var p0 = BinaryPrimitives.ReadUInt64LittleEndian(bytes[..8]);
            var p1 = BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(8, 8));
            return new AssetKey(p0, p1);
        }
    }
}
