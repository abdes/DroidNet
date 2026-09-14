// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Serialization;
using Json.Schema;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Portable native import settings stored beside the retained source.</summary>
/// <param name="SchemaVersion">The native sidecar version; legacy managed sidecars are not rewritten.</param>
/// <param name="Importer">The native importer contract identity.</param>
/// <param name="MountPoint">The authoring mount owning the generated output namespace.</param>
/// <param name="Name">The stable native scene/name namespace.</param>
/// <param name="BundleRoot">The project-relative retained source bundle.</param>
/// <param name="PrimaryRelativePath">The primary source relative to its bundle.</param>
/// <param name="SourceHash">The primary bytes used to discover the initial dependency set.</param>
/// <param name="Files">The discovered bundle-relative files, including the primary.</param>
/// <param name="OutputDirectory">The exclusively owned output directory relative to the mount.</param>
public sealed record NativeSceneImportSettings(
    int SchemaVersion,
    string Importer,
    string MountPoint,
    string Name,
    string BundleRoot,
    string PrimaryRelativePath,
    string SourceHash,
    ImmutableArray<string> Files,
    string OutputDirectory)
{
    /// <summary>The sidecar suffix already used for source import configuration.</summary>
    public const string SidecarSuffix = ".import.json";

    /// <summary>The native static/scalar importer contract.</summary>
    public const string ImporterIdentity = "Oxygen.Cooker.Scene/v1";

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly Lazy<JsonSchema> Schema = new(static () =>
    {
        using var stream = typeof(NativeSceneImportSettings).Assembly.GetManifestResourceStream("Oxygen.Editor.ContentPipeline.Import.Schemas.native-scene-import.schema.json")
            ?? throw new InvalidOperationException("The native source settings schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    });

    /// <summary>Gets the required supported source-content policy.</summary>
    public string ContentPolicy { get; init; } = "static-scalar";

    /// <summary>Gets the canonical meter conversion policy.</summary>
    public string UnitPolicy { get; init; } = "normalize";

    /// <summary>Gets a value indicating whether node transforms are baked; retained native imports preserve local transforms.</summary>
    public bool BakeTransforms { get; init; }

    /// <summary>Gets the normal preservation/generation policy.</summary>
    public string NormalsPolicy { get; init; } = "generate";

    /// <summary>Gets the tangent policy for the scalar-material subset.</summary>
    public string TangentsPolicy { get; init; } = "preserve";

    /// <summary>Gets the native output prefix; generated paths stay beneath it.</summary>
    [JsonIgnore]
    public string OutputPrefix => "/" + this.MountPoint + "/" + this.OutputDirectory.Trim('/') + "/";

    /// <summary>Creates settings from a retained bundle, preserving native naming and source identities.</summary>
    /// <param name="source">The retained source and hashes.</param>
    /// <param name="mountPoint">The selected authoring mount.</param>
    /// <param name="name">The stable import name.</param>
    /// <param name="outputDirectory">The mount-relative destination owned by this source.</param>
    /// <returns>The portable settings.</returns>
    public static NativeSceneImportSettings Create(RetainedImportSource source, string mountPoint, string name, string outputDirectory)
    {
        ArgumentNullException.ThrowIfNull(source);
        var settings = new NativeSceneImportSettings(
            2,
            ImporterIdentity,
            mountPoint,
            name,
            source.DirectoryRelativePath,
            source.PrimaryRelativePath,
            source.Files.Single(file => string.Equals(file.RelativePath, source.PrimaryRelativePath, StringComparison.Ordinal)).Sha256,
            source.Files.Select(static file => file.RelativePath).ToImmutableArray(),
            outputDirectory);
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
            throw new InvalidDataException("This source does not have valid native import settings. Import it into a new destination without replacing existing asset identities.");
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
        var root = Path.GetFullPath(projectRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var path = Path.GetFullPath(Path.Combine(root, this.BundleRoot, relative));
        return path.StartsWith(root, StringComparison.OrdinalIgnoreCase)
            ? path : throw new InvalidDataException("Import source settings resolve outside their project.");
    }

    private static void ValidatePaths(NativeSceneImportSettings settings)
    {
        var paths = settings.Files.Append(settings.BundleRoot).Append(settings.OutputDirectory).Append(settings.PrimaryRelativePath);
        if (paths.Any(static path => Path.IsPathRooted(path) || path.Split('/').Any(static part => string.IsNullOrEmpty(part)
                || part is "." or ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || part.EndsWith('.') || part.EndsWith(' ')))
            || settings.Files.ToHashSet(StringComparer.OrdinalIgnoreCase).Count != settings.Files.Length
            || !settings.Files.Contains(settings.PrimaryRelativePath, StringComparer.OrdinalIgnoreCase)
            || string.IsNullOrWhiteSpace(settings.MountPoint) || settings.MountPoint is "." or ".."
            || string.IsNullOrWhiteSpace(settings.Name) || settings.Name is "." or ".."
            || settings.MountPoint.EndsWith('.') || settings.MountPoint.EndsWith(' ')
            || settings.Name.EndsWith('.') || settings.Name.EndsWith(' ')
            || settings.MountPoint.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            || settings.Name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new InvalidDataException("Native import settings require contained source paths, a unique output directory and valid names.");
        }
    }
}
