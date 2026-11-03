// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Encodings.Web;
using System.Text.Json;
using Json.Schema;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>A model's import state: the editor's reader and writer of its model import record.</summary>
/// <remarks>
/// The record lives under <c>&lt;ProjectRoot&gt;/Imports/</c> at its model's virtual path, outside every authoring mount:
/// the model <c>/Content/Models/Ship.glb</c> has the record <c>Imports/Content/Models/Ship.glb.import.json</c>. It follows
/// the engine's <c>oxygen.model-import.schema.json</c>, which the ImportTool reads and writes too. It holds what to import
/// (the source, the model's virtual path, the settings) and what earlier imports established (the source identity, the
/// surface provenance, the node continuity, the published nodes, the source fingerprint). The source identity, a UUIDv7
/// the record holds, never changes. Relocating a model rewrites its <see cref="Source"/>; giving it another virtual path
/// moves the record to <see cref="PathFor"/> of the new path.
/// </remarks>
public sealed record ModelImportSettings
{
    /// <summary>The record version this editor reads and writes.</summary>
    public const int CurrentSchemaVersion = 1;

    /// <summary>The folder of the project root that holds the records.</summary>
    public const string RecordsFolder = "Imports";

    /// <summary>The suffix of a record's file name, after its model's file name.</summary>
    public const string RecordSuffix = AssetFileTypes.ImportSettings;

    private static readonly EngineSchemaSet Schemas = new(typeof(ModelImportSettings).Assembly);

    private static readonly Lazy<JsonSchema> Schema = new(static () => Schemas.Get("oxygen.model-import.schema.json"));

    private static readonly JsonWriterOptions WriterOptions = new()
    {
        Indented = true,
        NewLine = "\n",
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>Gets the primary source file by its mount path (<c>/Content/Car/Car.glb</c>), the same in every project
    /// that mounts its folder, wherever the folder is.</summary>
    public required string Source { get; init; }

    /// <summary>Gets the model's identity, under which its products are named (<c>/Content/Car/Car.glb</c>).</summary>
    public required string VirtualPath { get; init; }

    /// <summary>Gets the model import settings, a JSON object in the vocabulary of an import manifest's model job.</summary>
    public required JsonElement Settings { get; init; }

    /// <summary>Gets the identity from which the model's node and surface IDs are derived.</summary>
    public required Guid SourceIdentity { get; init; }

    /// <summary>Gets the engine's surface provenance, its identity left out; empty until the first import.</summary>
    public required JsonElement SurfaceProvenance { get; init; }

    /// <summary>Gets the node continuity entries: a node keeps the ID of an earlier anchor.</summary>
    public ImmutableArray<ModelNodeContinuity> NodeContinuity { get; init; } = [];

    /// <summary>Gets the anchor and ID of each node the last import emitted.</summary>
    public ImmutableArray<ModelPublishedNode> PublishedNodes { get; init; } = [];

    /// <summary>Gets the published nodes whose anchor the publication before the last did not emit.</summary>
    public ImmutableArray<ModelPublishedNode> NewNodes { get; init; } = [];

    /// <summary>Gets the nodes earlier imports emitted whose ID the last import did not, the nodes kept changes lost.</summary>
    public ImmutableArray<ModelPublishedNode> VanishedNodes { get; init; } = [];

    /// <summary>Gets the files the last import read; absent until the first import.</summary>
    public ModelSourceFingerprint? SourceFingerprint { get; init; }

    /// <summary>Gets the path of the record of the model <paramref name="virtualPath"/>.</summary>
    /// <param name="projectRoot">The project root, the records' content root.</param>
    /// <param name="virtualPath">The model's virtual path, such as <c>/Content/Models/Ship.glb</c>.</param>
    /// <returns>The record's full path, <c>&lt;ProjectRoot&gt;/Imports/Content/Models/Ship.glb.import.json</c>.</returns>
    /// <exception cref="ArgumentException"><paramref name="virtualPath"/> is not an absolute virtual path.</exception>
    public static string PathFor(string projectRoot, string virtualPath)
        => Path.Combine(Path.GetFullPath(projectRoot), RelativePathFor(virtualPath).Replace('/', Path.DirectorySeparatorChar));

    /// <summary>Gets the path of the record of the model <paramref name="virtualPath"/> below its project root.</summary>
    /// <param name="virtualPath">The model's virtual path, such as <c>/Content/Models/Ship.glb</c>.</param>
    /// <returns>The path with <c>/</c> separators, <c>Imports/Content/Models/Ship.glb.import.json</c>.</returns>
    /// <exception cref="ArgumentException"><paramref name="virtualPath"/> is not an absolute virtual path.</exception>
    public static string RelativePathFor(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        return IsMountPath(virtualPath)
            ? RecordsFolder + virtualPath + RecordSuffix
            : throw new ArgumentException($"A model import record is named by its model's virtual path, which '{virtualPath}' is not.", nameof(virtualPath));
    }

    /// <summary>Gets the project root a record belongs to, from its path and the virtual path it holds.</summary>
    /// <param name="recordPath">A record's path.</param>
    /// <param name="virtualPath">The virtual path the record holds.</param>
    /// <returns>The project root, or <see langword="null"/> when the record is not at the place of its virtual path.</returns>
    public static string? ProjectRootOf(string recordPath, string virtualPath)
    {
        var path = Path.GetFullPath(recordPath);
        string relative;
        try
        {
            relative = RelativePathFor(virtualPath).Replace('/', Path.DirectorySeparatorChar);
        }
        catch (ArgumentException)
        {
            return null;
        }

        return path.Length > relative.Length
            && path.EndsWith(relative, StringComparison.OrdinalIgnoreCase)
            && Path.EndsInDirectorySeparator(path[..^relative.Length])
                ? Path.TrimEndingDirectorySeparator(path[..^relative.Length])
                : null;
    }

    /// <summary>Gets the settings of a new model: its name, units converted to meters, node transforms kept, and normals and
    /// tangents generated where the file has none.</summary>
    /// <param name="name">The import's name, shown for its cook job.</param>
    /// <returns>The settings, in the import manifest's vocabulary.</returns>
    public static JsonElement DefaultSettings(string name)
        => JsonSerializer.SerializeToElement(new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["name"] = name,
            ["unit_policy"] = "normalize",
            ["bake_transforms"] = false,
            ["normals_policy"] = "generate",
            ["tangents_policy"] = "generate",
        });

    /// <summary>Creates the record of a model before its first import.</summary>
    /// <param name="recordPath">Where the record will live, <see cref="PathFor"/> of <paramref name="virtualPath"/>.</param>
    /// <param name="source">The mount path of the model's primary source file, such as <c>/Content/Car/Car.glb</c>.</param>
    /// <param name="virtualPath">The model's identity.</param>
    /// <param name="settings">The model import settings.</param>
    /// <param name="sourceIdentity">The model's source identity, a UUIDv7; a new one when omitted.</param>
    /// <returns>The record.</returns>
    /// <exception cref="ArgumentException">The record path is not the place of <paramref name="virtualPath"/>, the source
    /// is not a mount path, or the identity is not a UUIDv7.</exception>
    public static ModelImportSettings Create(string recordPath, string source, string virtualPath, JsonElement settings, Guid? sourceIdentity = null)
    {
        _ = ProjectRootOf(recordPath, virtualPath)
            ?? throw new ArgumentException($"A model import record lives at the place of its virtual path '{virtualPath}'.", nameof(recordPath));
        var identity = sourceIdentity ?? Guid.CreateVersion7();
        if (identity.Version != 7)
        {
            throw new ArgumentException("A model's source identity is a UUIDv7.", nameof(sourceIdentity));
        }

        var record = new ModelImportSettings
        {
            Source = string.Empty,
            VirtualPath = virtualPath,
            Settings = settings.Clone(),
            SourceIdentity = identity,
            SurfaceProvenance = EmptyProvenance(),
        }.WithSource(source);
        _ = record.ToBytes();
        return record;
    }

    /// <summary>Reads and validates a record against the installed engine schema.</summary>
    /// <param name="bytes">The record's bytes.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">The bytes are not a valid model import record.</exception>
    public static ModelImportSettings Parse(ReadOnlyMemory<byte> bytes)
    {
        using var document = JsonDocument.Parse(bytes);
        var root = document.RootElement;
        if (!Schema.Value.Evaluate(root).IsValid)
        {
            throw new InvalidDataException($"This is not a valid model import record of version {CurrentSchemaVersion}.");
        }

        var continuity = root.TryGetProperty("node_continuity", out var entries)
            ? [.. entries.EnumerateArray().Select(static entry => new ModelNodeContinuity(
                entry.GetProperty("anchor").GetString()!, entry.GetProperty("continues").GetString()!))]
            : ImmutableArray<ModelNodeContinuity>.Empty;
        if (continuity.Select(static entry => entry.Anchor).Distinct(StringComparer.Ordinal).Take(continuity.Length + 1).Count() != continuity.Length)
        {
            throw new InvalidDataException("Two node continuity entries of the model import record have one anchor.");
        }

        var source = root.GetProperty("source").GetString()!;
        if (!IsMountPath(source))
        {
            throw new InvalidDataException($"The model import record names its source '{source}', which is not a mount path.");
        }

        return new()
        {
            Source = source,
            VirtualPath = root.GetProperty("virtual_path").GetString()!,
            Settings = root.GetProperty("settings").Clone(),
            SourceIdentity = root.GetProperty("source_identity").GetGuid(),
            SurfaceProvenance = root.TryGetProperty("surface_provenance", out var provenance) ? provenance.Clone() : EmptyProvenance(),
            NodeContinuity = continuity,
            PublishedNodes = ReadNodes(root, "published_nodes"),
            NewNodes = ReadNodes(root, "new_nodes"),
            VanishedNodes = ReadNodes(root, "vanished_nodes"),
            SourceFingerprint = root.TryGetProperty("source_fingerprint", out var fingerprint)
                ? ModelSourceFingerprint.Read(fingerprint) : null,
        };
    }

    /// <summary>Reads a record from its file, which must be at the place of its virtual path.</summary>
    /// <param name="recordPath">The record's path.</param>
    /// <param name="bytes">The record's bytes.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">The record is invalid or not at its virtual path.</exception>
    public static ModelImportSettings Parse(string recordPath, ReadOnlyMemory<byte> bytes)
    {
        var record = Parse(bytes);
        return ProjectRootOf(recordPath, record.VirtualPath) is not null
            ? record
            : throw new InvalidDataException($"The model import record '{recordPath}' is not at the place of its virtual path '{record.VirtualPath}'.");
    }

    /// <summary>Gets the record's source file, through the project's mount table.</summary>
    /// <param name="project">The project and its mount table.</param>
    /// <returns>The source file, or <see langword="null"/> when its mount is not a bound source mount on this
    /// machine.</returns>
    public FileSystemPath? ResolveSource(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return project.SourceFileOf(this.Source);
    }

    /// <summary>Gets the record with its source rewritten, as relocating the model does; nothing else changes.</summary>
    /// <param name="source">The mount path of the source's new place, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The rebound record.</returns>
    /// <exception cref="ArgumentException">The source is not a mount path.</exception>
    public ModelImportSettings WithSource(string source)
        => IsMountPath(source)
            ? this with { Source = source }
            : throw new ArgumentException($"A model import record names its source by its mount path, which '{source}' is not.", nameof(source));

    /// <summary>Gets the record with what an import established: the <c>model_import</c> of the engine's report, its
    /// surface provenance, published, new and vanished nodes and source fingerprint. Nothing else changes.</summary>
    /// <param name="update">The report's <c>model_import</c> object.</param>
    /// <returns>The updated record.</returns>
    public ModelImportSettings WithImportUpdate(JsonElement update)
    {
        var updated = this with
        {
            SurfaceProvenance = update.GetProperty("surface_provenance").Clone(),
            PublishedNodes = ReadNodes(update, "published_nodes"),
            NewNodes = ReadNodes(update, "new_nodes"),
            VanishedNodes = ReadNodes(update, "vanished_nodes"),
            SourceFingerprint = update.TryGetProperty("source_fingerprint", out var fingerprint)
                ? ModelSourceFingerprint.Read(fingerprint) : this.SourceFingerprint,
        };
        _ = updated.ToBytes();
        return updated;
    }

    /// <summary>Gets whether <paramref name="other"/> differs from this record only in what an import establishes.</summary>
    /// <param name="other">Another version of this record.</param>
    /// <returns><see langword="true"/> when only the surface provenance, the published, new or vanished nodes or the
    /// fingerprint differ.</returns>
    public bool DiffersOnlyByImport(ModelImportSettings other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var aligned = other with
        {
            SurfaceProvenance = this.SurfaceProvenance,
            PublishedNodes = this.PublishedNodes,
            NewNodes = this.NewNodes,
            VanishedNodes = this.VanishedNodes,
            SourceFingerprint = this.SourceFingerprint,
        };
        return aligned.ToBytes().AsSpan().SequenceEqual(this.ToBytes());
    }

    /// <summary>Gets the record with a node continuity entry: the node with <paramref name="anchor"/> continues the node
    /// with <paramref name="continues"/>, and takes its ID on the next import. Nothing else changes.</summary>
    /// <param name="anchor">The continuing node's anchor.</param>
    /// <param name="continues">The anchor of the node it continues.</param>
    /// <returns>The record with the entry.</returns>
    /// <exception cref="ArgumentException">The anchors are equal, or <paramref name="anchor"/> already continues a node.</exception>
    public ModelImportSettings WithContinuity(string anchor, string continues)
    {
        ArgumentException.ThrowIfNullOrEmpty(anchor);
        ArgumentException.ThrowIfNullOrEmpty(continues);
        if (string.Equals(anchor, continues, StringComparison.Ordinal))
        {
            throw new ArgumentException("A node cannot continue itself.", nameof(continues));
        }

        if (this.NodeContinuity.Any(entry => string.Equals(entry.Anchor, anchor, StringComparison.Ordinal)))
        {
            throw new ArgumentException($"'{anchor}' already continues a node.", nameof(anchor));
        }

        var updated = this with { NodeContinuity = this.NodeContinuity.Add(new ModelNodeContinuity(anchor, continues)) };
        _ = updated.ToBytes();
        return updated;
    }

    /// <summary>Gets the record without the node continuity entry of <paramref name="anchor"/>: the node gets its own ID
    /// again on the next import. Nothing else changes.</summary>
    /// <param name="anchor">The continuing node's anchor.</param>
    /// <returns>The record without the entry.</returns>
    public ModelImportSettings WithoutContinuity(string anchor)
    {
        ArgumentException.ThrowIfNullOrEmpty(anchor);
        return this with { NodeContinuity = this.NodeContinuity.RemoveAll(entry => string.Equals(entry.Anchor, anchor, StringComparison.Ordinal)) };
    }

    /// <summary>Gets the record with the model import settings replaced. Nothing else changes.</summary>
    /// <param name="settings">The settings, in the import manifest's vocabulary.</param>
    /// <returns>The record with the settings.</returns>
    /// <exception cref="InvalidDataException">The settings do not match the schema.</exception>
    public ModelImportSettings WithSettings(JsonElement settings)
    {
        var updated = this with { Settings = settings.Clone() };
        _ = updated.ToBytes();
        return updated;
    }

    /// <summary>Gets <paramref name="latest"/>, a newer version of this record read from its file, with this record's
    /// settings and node continuity: what the user edits, kept over what an import or a relocation rewrote.</summary>
    /// <param name="latest">The record as its file holds it now.</param>
    /// <returns>The merged record.</returns>
    public ModelImportSettings WithEditsOver(ModelImportSettings latest)
    {
        ArgumentNullException.ThrowIfNull(latest);
        return latest with { Settings = this.Settings, NodeContinuity = this.NodeContinuity };
    }

    /// <summary>Serializes the record, validated against the installed schema.</summary>
    /// <returns>The record's complete bytes.</returns>
    public byte[] ToBytes()
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, WriterOptions))
        {
            writer.WriteStartObject();
            writer.WriteNumber("schema_version", CurrentSchemaVersion);
            writer.WriteString("source", this.Source);
            writer.WriteString("virtual_path", this.VirtualPath);
            writer.WritePropertyName("settings");
            this.Settings.WriteTo(writer);
            writer.WriteString("source_identity", this.SourceIdentity.ToString("D"));
            writer.WritePropertyName("surface_provenance");
            this.SurfaceProvenance.WriteTo(writer);
            writer.WriteStartArray("node_continuity");
            foreach (var entry in this.NodeContinuity)
            {
                writer.WriteStartObject();
                writer.WriteString("anchor", entry.Anchor);
                writer.WriteString("continues", entry.Continues);
                writer.WriteEndObject();
            }

            writer.WriteEndArray();
            WriteNodes(writer, "published_nodes", this.PublishedNodes);
            WriteNodes(writer, "new_nodes", this.NewNodes);
            WriteNodes(writer, "vanished_nodes", this.VanishedNodes);
            this.SourceFingerprint?.Write(writer);
            writer.WriteEndObject();
        }

        stream.WriteByte((byte)'\n');
        var bytes = stream.ToArray();
        using var document = JsonDocument.Parse(bytes);
        return Schema.Value.Evaluate(document.RootElement).IsValid
            ? bytes
            : throw new InvalidDataException("The model import record does not match its schema.");
    }

    private static ImmutableArray<ModelPublishedNode> ReadNodes(JsonElement owner, string name)
        => owner.TryGetProperty(name, out var nodes)
            ? [.. nodes.EnumerateArray().Select(static node => new ModelPublishedNode(
                node.GetProperty("anchor").GetString()!, node.GetProperty("node_id").GetGuid()))]
            : [];

    private static void WriteNodes(Utf8JsonWriter writer, string name, ImmutableArray<ModelPublishedNode> nodes)
    {
        writer.WriteStartArray(name);
        foreach (var node in nodes)
        {
            writer.WriteStartObject();
            writer.WriteString("anchor", node.Anchor);
            writer.WriteString("node_id", node.NodeId.ToString("D"));
            writer.WriteEndObject();
        }

        writer.WriteEndArray();
    }

    // A mount path, or a virtual path: '/', a mount name, then '/'-separated names, never '.' or '..'.
    private static bool IsMountPath(string path)
        => path.Length >= 2 && path[0] == '/' && !path.Contains('\\', StringComparison.Ordinal)
            && path[1..].Split('/').All(static segment => segment is not ("" or "." or ".."));

    private static JsonElement EmptyProvenance()
        => JsonSerializer.SerializeToElement(new { schema_version = 2, geometries = Array.Empty<object>() });
}
