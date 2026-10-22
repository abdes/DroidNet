// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Portable native import settings stored beside the retained source.</summary>
/// <remarks>
/// The model is identified by the virtual path of its primary file, and its products are named under that identity
/// (<c>/Content/Ship/Ship.glb/Paint.omat</c>); the settings name no output location. An older settings version is
/// refused, not migrated.
/// </remarks>
/// <param name="SchemaVersion">The native sidecar version; older versions are refused.</param>
/// <param name="Importer">The native importer contract identity.</param>
/// <param name="Name">The import's name, shown for its cook job.</param>
/// <param name="BundleRoot">The project-relative retained source bundle.</param>
/// <param name="PrimaryRelativePath">The primary source relative to its bundle.</param>
/// <param name="SourceHash">The primary bytes used to discover the initial dependency set.</param>
/// <param name="Files">The discovered bundle-relative files, including the primary.</param>
public sealed record NativeSceneImportSettings(
    int SchemaVersion,
    string Importer,
    string Name,
    string BundleRoot,
    string PrimaryRelativePath,
    string SourceHash,
    ImmutableArray<string> Files)
{
    /// <summary>The settings version this editor writes and reads.</summary>
    public const int CurrentSchemaVersion = 5;

    /// <summary>The sidecar suffix already used for source import configuration.</summary>
    public const string SidecarSuffix = AssetFileTypes.ImportSettings;

    /// <summary>The native scene importer contract.</summary>
    public const string ImporterIdentity = "Oxygen.Cooker.Scene/v1";

    /// <summary>The supported editor model policy.</summary>
    public const string DefaultContentPolicy = "static";

    /// <summary>The native unit conversion default.</summary>
    public const string DefaultUnitPolicy = "normalize";

    /// <summary>The native normal-generation default.</summary>
    public const string DefaultNormalsPolicy = "generate";

    /// <summary>The native policy that preserves authored tangents and generates missing ones.</summary>
    public const string DefaultTangentsPolicy = "generate";

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly Lazy<JsonSchema> Schema = new(static () =>
    {
        using var stream = typeof(NativeSceneImportSettings).Assembly.GetManifestResourceStream("Oxygen.Editor.ContentPipeline.Import.Schemas.native-scene-import.schema.json")
            ?? throw new InvalidOperationException("The native source settings schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    });

    /// <summary>Gets the native identities published with this source's geometry.</summary>
    public required NativeSurfaceProvenance SurfaceProvenance { get; init; }

    /// <summary>Gets the required supported source-content policy.</summary>
    public string ContentPolicy { get; init; } = DefaultContentPolicy;

    /// <summary>Gets the canonical meter conversion policy.</summary>
    public string UnitPolicy { get; init; } = DefaultUnitPolicy;

    /// <summary>Gets a value indicating whether node transforms are baked; retained native imports preserve local transforms.</summary>
    public bool BakeTransforms { get; init; }

    /// <summary>Gets the normal preservation/generation policy.</summary>
    public string NormalsPolicy { get; init; } = DefaultNormalsPolicy;

    /// <summary>Gets the native tangent preservation or generation policy.</summary>
    public string TangentsPolicy { get; init; } = DefaultTangentsPolicy;

    /// <summary>Creates settings from a retained bundle, preserving native naming and source identities.</summary>
    /// <param name="source">The retained source and hashes.</param>
    /// <param name="name">The import's name.</param>
    /// <returns>The portable settings.</returns>
    public static NativeSceneImportSettings Create(RetainedImportSource source, string name)
    {
        ArgumentNullException.ThrowIfNull(source);
        var settings = new NativeSceneImportSettings(
            CurrentSchemaVersion,
            ImporterIdentity,
            name,
            source.DirectoryRelativePath,
            source.PrimaryRelativePath,
            source.Files.Single(file => string.Equals(file.RelativePath, source.PrimaryRelativePath, StringComparison.Ordinal)).Sha256,
            source.Files.Select(static file => file.RelativePath).ToImmutableArray())
        {
            SurfaceProvenance = NativeSurfaceProvenance.Create(),
        };
        _ = settings.ToBytes();
        return settings;
    }

    /// <summary>Validates persisted native settings without accepting legacy managed identity metadata.</summary>
    /// <param name="bytes">The saved sidecar bytes.</param>
    /// <returns>The validated native settings.</returns>
    public static NativeSceneImportSettings Parse(ReadOnlyMemory<byte> bytes)
    {
        using var document = JsonDocument.Parse(bytes);
        if (!Schema.Value.Evaluate(document.RootElement).IsValid)
        {
            throw new InvalidDataException($"This source does not have valid native import settings of version {CurrentSchemaVersion}. Import it again into a new destination.");
        }

        var settings = JsonSerializer.Deserialize<NativeSceneImportSettings>(bytes.Span, JsonOptions)!;
        ValidatePaths(settings);
        return settings;
    }

    /// <summary>Serializes schema-valid settings for an ordinary atomic source write.</summary>
    /// <returns>The complete sidecar contents.</returns>
    public byte[] ToBytes()
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(this, JsonOptions);
        _ = Parse(bytes);
        return bytes;
    }

    /// <summary>Creates retained settings atomically without replacing an existing source identity contract.</summary>
    /// <param name="projectRoot">The owning project.</param>
    /// <param name="files">The ordinary atomic file store.</param>
    /// <param name="cancellationToken">Cancels before the sidecar is committed.</param>
    /// <returns>The committed settings version.</returns>
    public Task<DroidNet.Storage.FileVersion> SaveNewAsync(string projectRoot, DroidNet.Storage.IAtomicFileStore files, CancellationToken cancellationToken)
        => files.WriteAsync(this.ResolveFile(projectRoot, this.PrimaryRelativePath) + SidecarSuffix, this.ToBytes(), DroidNet.Storage.FileVersion.Missing, cancellationToken);

    /// <summary>Resolves a bundle path without allowing it to escape the project.</summary>
    /// <param name="projectRoot">The owning project.</param>
    /// <param name="relative">A validated bundle-relative file.</param>
    /// <returns>The original source location.</returns>
    public string ResolveFile(string projectRoot, string relative)
    {
        var root = FileSystemPath.From(projectRoot);
        var path = root.Combine(Path.Combine(this.BundleRoot, relative));
        return path != root && path.IsSameOrInside(root)
            ? path.FullPath : throw new InvalidDataException("Import source settings resolve outside their project.");
    }

    private static void ValidatePaths(NativeSceneImportSettings settings)
    {
        // The source files are physical paths inside the retained bundle; the name is a virtual path segment,
        // checked by the virtual path rules.
        var files = settings.Files.Append(settings.BundleRoot).Append(settings.PrimaryRelativePath);
        if (files.Any(static path => Path.IsPathRooted(path) || path.Split('/').Any(static part => string.IsNullOrEmpty(part)
                || part is "." or ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || part.EndsWith('.') || part.EndsWith(' ')))
            || settings.Files.ToHashSet(FileSystemPath.NameComparer).Count != settings.Files.Length
            || !settings.Files.Contains(settings.PrimaryRelativePath, FileSystemPath.NameComparer)
            || VirtualPath.ValidateSegment(settings.Name, VirtualPathRuleSet.Authored) is not null)
        {
            throw new InvalidDataException("Native import settings require contained source paths and a valid name.");
        }
    }
}
