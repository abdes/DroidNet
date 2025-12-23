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

        this.importer = CreateImporterData(importer, options);
    }

    /// <inheritdoc />
    public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(virtualPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(assetType);

        // This policy is synchronous by interface contract.
        this.cached ??= this.LoadOrCreateSidecarAsync().GetAwaiter().GetResult();

        if (TryUpdateImporterMetadata(this.cached, this.importer, out var updated))
        {
            this.cached = updated;
            this.SaveSidecarAsync(this.cached).GetAwaiter().GetResult();
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

        this.SaveSidecarAsync(this.cached).GetAwaiter().GetResult();
        return created;
    }

    private static async Task<SidecarData> CreateNewSidecarAsync(IImportFileAccess files, ImportInput input)
    {
        var sourceBytes = await files.ReadAllBytesAsync(input.SourcePath).ConfigureAwait(false);
        var sha256 = SHA256.HashData(sourceBytes.Span);

        return new SidecarData(
            SchemaVersion: SchemaVersion,
            MountPoint: input.MountPoint,
            Source: new SidecarSourceData(
                RelativePath: input.SourcePath,
                LastWriteTimeUtc: DateTimeOffset.UtcNow,
                Sha256: Convert.ToHexString(sha256),
                ByteLength: sourceBytes.Length),
            Importer: new SidecarImporterData(
                Name: "(unknown)",
                Type: null,
                Version: "0",
                Settings: new Dictionary<string, JsonElement>(StringComparer.Ordinal)),
            Outputs: []);
    }

    private static string GetSidecarPath(string sourcePath) => sourcePath + ".import.json";

    private static SidecarImporterData CreateImporterData(IAssetImporter importer, ImportOptions options)
    {
        var version = importer.GetType().Assembly.GetName().Version?.ToString() ?? "0";

        var settings = new Dictionary<string, JsonElement>(StringComparer.Ordinal)
        {
            ["FailFast"] = JsonSerializer.SerializeToElement(options.FailFast),
            ["ReimportIfUnchanged"] = JsonSerializer.SerializeToElement(options.ReimportIfUnchanged),
            ["LogLevel"] = JsonSerializer.SerializeToElement(options.LogLevel.ToString()),
        };

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

    private async Task<SidecarData> LoadOrCreateSidecarAsync()
    {
        var sidecarPath = GetSidecarPath(this.input.SourcePath);

        try
        {
            var bytes = await this.files.ReadAllBytesAsync(sidecarPath).ConfigureAwait(false);
            var data = JsonSerializer.Deserialize(bytes.Span, SidecarSerializationContext.Default.SidecarData)
                ?? throw new InvalidDataException("Sidecar JSON is empty or invalid.");

            return data;
        }
        catch (FileNotFoundException)
        {
            var initial = await CreateNewSidecarAsync(this.files, this.input).ConfigureAwait(false);
            await this.SaveSidecarAsync(initial).ConfigureAwait(false);
            return initial;
        }
    }

    private async Task SaveSidecarAsync(SidecarData data)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(data, SidecarSerializationContext.Default.SidecarData);
        await this.files.WriteAllBytesAsync(GetSidecarPath(this.input.SourcePath), bytes).ConfigureAwait(false);
    }

    internal sealed record SidecarData(
        int SchemaVersion,
        string MountPoint,
        SidecarSourceData Source,
        SidecarImporterData Importer,
        List<SidecarOutputData> Outputs);

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

    [JsonSourceGenerationOptions(
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
        AllowTrailingCommas = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow)]
    [JsonSerializable(typeof(SidecarData))]
    internal sealed partial class SidecarSerializationContext : JsonSerializerContext
    {
    }

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
