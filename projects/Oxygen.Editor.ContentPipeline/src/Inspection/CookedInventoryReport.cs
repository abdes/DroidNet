// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Native index metadata and observed integrity failures for one protected opening.</summary>
public sealed class CookedInventoryReport
{
    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);

    private CookedInventoryReport(
        Guid sourceKey,
        long indexSize,
        string indexSha256,
        ImmutableDictionary<string, CookedInventoryMember> files,
        ImmutableArray<CookedInventoryAsset> assets,
        ImmutableArray<CookedInventoryResource> resources,
        ImmutableArray<CookedInventoryIssue> issues)
    {
        this.SourceKey = sourceKey;
        this.IndexSize = indexSize;
        this.IndexSha256 = indexSha256;
        this.Files = files;
        this.Assets = assets;
        this.Resources = resources;
        this.Issues = issues;
    }

    /// <summary>Gets the native source identity.</summary>
    public Guid SourceKey { get; }

    /// <summary>Gets the protected index length.</summary>
    public long IndexSize { get; }

    /// <summary>Gets the digest that selects this exact inventory.</summary>
    public string IndexSha256 { get; }

    /// <summary>Gets all expected content members, excluding the index and generation lock.</summary>
    public ImmutableDictionary<string, CookedInventoryMember> Files { get; }

    /// <summary>Gets the index's asset identities and descriptor locations.</summary>
    public ImmutableArray<CookedInventoryAsset> Assets { get; }

    /// <summary>Gets native resource descriptors, distinct from keyed runtime assets.</summary>
    public ImmutableArray<CookedInventoryResource> Resources { get; }

    /// <summary>Gets observed file failures; process success alone does not establish integrity.</summary>
    public ImmutableArray<CookedInventoryIssue> Issues { get; }

    /// <summary>Gets a value indicating whether every indexed member and the complete membership matched.</summary>
    public bool IsValid => this.Issues.IsEmpty;

    /// <summary>Reads the native schema and checks cross-record identities.</summary>
    /// <param name="json">The Inspector report.</param>
    /// <returns>The detached inventory snapshot.</returns>
    public static CookedInventoryReport Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (!Schema.Value.Evaluate(root).IsValid)
        {
            throw new InvalidDataException("The native integrity inventory does not match its schema.");
        }

        var files = ReadFiles(root);
        var paths = new HashSet<string>(StringComparer.Ordinal);
        var descriptors = new HashSet<string>(StringComparer.Ordinal);
        var assets = ReadAssets(root, files, paths, descriptors);
        var resources = ReadResources(root, files, paths, descriptors);
        var issues = ReadIssues(root);

        if (issues.IsEmpty && resources.Any(static resource => resource.ResourceIndex is null))
        {
            throw new InvalidDataException("The native inventory omitted a verified resource identity.");
        }

        return new(
            root.GetProperty("source_key").GetGuid(),
            root.GetProperty("index_size").GetInt64(),
            root.GetProperty("index_sha256").GetString()!,
            files,
            assets,
            resources,
            issues);
    }

    /// <summary>Creates the existing inspection view from the same native verification.</summary>
    /// <param name="root">The physical root for diagnostics.</param>
    /// <returns>Asset/file metadata and integrity diagnostics.</returns>
    public CookInspectionResult ToInspection(string root)
    {
        CookedAssetEntry[] assets = this.IsValid ? [.. this.Assets.Select(static asset => ToEntry(asset))] : [];
        var files = this.Files.Select(static file => new CookedFileEntry(file.Key, checked((ulong)file.Value.Size))).ToArray();
        return new(root, this.IsValid, this.SourceKey, assets, files, this.Diagnostics(root));
    }

    /// <summary>Creates the validation view without another native launch or hash pass.</summary>
    /// <param name="root">The physical root for diagnostics.</param>
    /// <returns>The integrity result.</returns>
    public CookValidationResult ToValidation(string root) => new(root, this.IsValid, this.Diagnostics(root));

    private static CookedAssetEntry ToEntry(CookedInventoryAsset asset)
    {
        var kind = asset.Type switch
        {
            1 => ContentCookAssetKind.Material,
            2 => ContentCookAssetKind.Geometry,
            3 => ContentCookAssetKind.Scene,
            _ => ContentCookAssetKind.Unknown,
        };
        return new CookedAssetEntry(asset.VirtualPath, kind)
        {
            DescriptorRelativePath = asset.DescriptorPath,
            AssetKey = asset.Key.ToString(),
        };
    }

    private static ImmutableDictionary<string, CookedInventoryMember> ReadFiles(JsonElement root)
    {
        var files = ImmutableDictionary.CreateBuilder<string, CookedInventoryMember>(StringComparer.Ordinal);
        foreach (var row in root.GetProperty("files").EnumerateArray())
        {
            var path = row.GetProperty("relative_path").GetString()!;
            ValidatePath(path);
            var kind = row.GetProperty("file_kind");
            var member = new CookedInventoryMember(
                row.GetProperty("size").GetInt64(),
                row.GetProperty("sha256").GetString()!,
                kind.ValueKind == JsonValueKind.Null ? null : (FileKind)kind.GetUInt16());
            if (!files.TryAdd(path, member))
            {
                throw new InvalidDataException("The native inventory contains duplicate member paths.");
            }
        }

        return files.ToImmutable();
    }

    private static ImmutableArray<CookedInventoryAsset> ReadAssets(
        JsonElement root,
        ImmutableDictionary<string, CookedInventoryMember> files,
        HashSet<string> paths,
        HashSet<string> descriptors)
    {
        var keys = new HashSet<Guid>();
        var assets = ImmutableArray.CreateBuilder<CookedInventoryAsset>();
        foreach (var row in root.GetProperty("assets").EnumerateArray())
        {
            var key = row.GetProperty("asset_key").GetGuid();
            var path = row.GetProperty("virtual_path").GetString()!;
            var descriptor = row.GetProperty("descriptor_relative_path").GetString()!;
            if (!keys.Add(key) || !paths.Add(path) || !descriptors.Add(descriptor) || !files.ContainsKey(descriptor))
            {
                throw new InvalidDataException("The native inventory contains ambiguous or unindexed asset descriptors.");
            }

            assets.Add(new(key, row.GetProperty("asset_type").GetByte(), path, descriptor));
        }

        return assets.ToImmutable();
    }

    private static ImmutableArray<CookedInventoryResource> ReadResources(
        JsonElement root,
        ImmutableDictionary<string, CookedInventoryMember> files,
        HashSet<string> paths,
        HashSet<string> descriptors)
    {
        var resources = ImmutableArray.CreateBuilder<CookedInventoryResource>();
        foreach (var row in root.GetProperty("resources").EnumerateArray())
        {
            var descriptor = row.GetProperty("descriptor_relative_path").GetString()!;
            if (!descriptors.Add(descriptor) || !files.ContainsKey(descriptor))
            {
                throw new InvalidDataException("The native inventory contains ambiguous or unindexed resource descriptors.");
            }

            var path = row.GetProperty("virtual_path").GetString()!;
            if (!paths.Add(path))
            {
                throw new InvalidDataException("The native inventory contains ambiguous resource virtual paths.");
            }

            var index = row.GetProperty("resource_index");
            resources.Add(new(row.GetProperty("kind").GetString()!, path, descriptor, index.ValueKind == JsonValueKind.Null ? null : index.GetUInt32()));
        }

        return resources.ToImmutable();
    }

    private static ImmutableArray<CookedInventoryIssue> ReadIssues(JsonElement root)
    {
        var issues = root.GetProperty("issues").EnumerateArray()
            .Select(static row => new CookedInventoryIssue(row.GetProperty("relative_path").GetString()!, row.GetProperty("reason").GetString()!))
            .ToImmutableArray();
        foreach (var issue in issues)
        {
            ValidatePath(issue.RelativePath);
        }

        return issues;
    }

    private static void ValidatePath(string path)
    {
        if (path.Contains('\\', StringComparison.Ordinal) || path.Contains(':', StringComparison.Ordinal)
            || path.Split('/').Any(static segment => segment is "" or "." or "..")
            || path is "container.index.bin" or ".generation.lock")
        {
            throw new InvalidDataException($"The native inventory contains a non-content path: {path}.");
        }
    }

    private static JsonSchema LoadSchema()
    {
        using var stream = typeof(CookedInventoryReport).Assembly.GetManifestResourceStream("oxygen.cooked-inventory.schema.json")
            ?? throw new InvalidOperationException("The native cooked inventory schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    }

    private DiagnosticRecord[] Diagnostics(string root) => [.. this.Issues.Select(issue => new DiagnosticRecord
    {
        OperationId = Guid.NewGuid(),
        Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
        Code = ContentPipelineDiagnosticCodes.ValidateFailed,
        Message = $"Cooked content integrity failed: {issue.RelativePath} ({issue.Reason}).",
        AffectedPath = Path.Combine(root, issue.RelativePath),
    })];
}
