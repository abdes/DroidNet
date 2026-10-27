// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Encodings.Web;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Models;

/// <summary>A model's import state: the editor's reader and writer of its model import record.</summary>
/// <remarks>
/// The record lives at <c>&lt;ProjectRoot&gt;/imports/&lt;source_identity&gt;.import.json</c>, outside every authoring
/// mount, against the engine's <c>oxygen.model-import.schema.json</c>, which the ImportTool reads and writes too. It
/// holds what to import (the source, the model's virtual path, the settings) and what earlier imports established (the
/// source identity, the surface provenance, the node continuity, the published nodes, the source fingerprint). The source
/// identity never changes, so a record is never renamed: relocating a model rewrites its <see cref="Source"/>.
/// </remarks>
public sealed record ModelImportSettings
{
    /// <summary>The record version this editor reads and writes.</summary>
    public const int CurrentSchemaVersion = 1;

    /// <summary>The folder of the project root that holds the records.</summary>
    public const string RecordsFolder = "imports";

    /// <summary>The suffix of a record's file name, after its source identity.</summary>
    public const string RecordSuffix = AssetFileTypes.ImportSettings;

    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);

    private static readonly JsonWriterOptions WriterOptions = new()
    {
        Indented = true,
        NewLine = "\n",
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>Gets the primary source file, relative to the record's folder (<c>../Content/Car/Car.glb</c>), or absolute
    /// when the source is outside the project.</summary>
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

    /// <summary>Gets the path of the record of <paramref name="sourceIdentity"/>.</summary>
    /// <param name="projectRoot">The project root, the records' content root.</param>
    /// <param name="sourceIdentity">The model's source identity.</param>
    /// <returns>The record's full path.</returns>
    public static string PathFor(string projectRoot, Guid sourceIdentity)
        => Path.Combine(Path.GetFullPath(projectRoot), RecordsFolder, sourceIdentity.ToString("D") + RecordSuffix);

    /// <summary>Gets the source identity a record's file name states.</summary>
    /// <param name="recordPath">A record's path.</param>
    /// <returns>The identity, or <see langword="null"/> when the name is not a record's.</returns>
    public static Guid? IdentityOf(string recordPath)
    {
        var name = Path.GetFileName(recordPath);
        return name.EndsWith(RecordSuffix, StringComparison.OrdinalIgnoreCase)
            && Guid.TryParseExact(name[..^RecordSuffix.Length], "D", out var identity) && identity.Version == 7
                ? identity : null;
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
    /// <param name="recordPath">Where the record will live; it names its source relative to it.</param>
    /// <param name="sourcePath">The model's primary source file.</param>
    /// <param name="virtualPath">The model's identity.</param>
    /// <param name="settings">The model import settings.</param>
    /// <returns>The record, its source identity from its path.</returns>
    public static ModelImportSettings Create(string recordPath, string sourcePath, string virtualPath, JsonElement settings)
    {
        var identity = IdentityOf(recordPath)
            ?? throw new ArgumentException("A model import record is named by a UUIDv7 source identity.", nameof(recordPath));
        var record = new ModelImportSettings
        {
            Source = string.Empty,
            VirtualPath = virtualPath,
            Settings = settings.Clone(),
            SourceIdentity = identity,
            SurfaceProvenance = EmptyProvenance(),
        }.WithSource(recordPath, sourcePath);
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

        return new()
        {
            Source = root.GetProperty("source").GetString()!,
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

    /// <summary>Reads a record from its file, which must be named by its source identity.</summary>
    /// <param name="recordPath">The record's path.</param>
    /// <param name="bytes">The record's bytes.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">The record is invalid or not named by its identity.</exception>
    public static ModelImportSettings Parse(string recordPath, ReadOnlyMemory<byte> bytes)
    {
        var record = Parse(bytes);
        return IdentityOf(recordPath) == record.SourceIdentity
            ? record
            : throw new InvalidDataException($"The model import record '{recordPath}' is not named by its source identity {record.SourceIdentity:D}.");
    }

    /// <summary>Gets the full path of the record's source.</summary>
    /// <param name="recordPath">The record's path.</param>
    /// <returns>The source's full path.</returns>
    public string ResolveSource(string recordPath)
        => Path.GetFullPath(Path.IsPathRooted(this.Source)
            ? this.Source
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(recordPath))!, this.Source));

    /// <summary>Gets the record with its source rewritten, as relocating the model does; nothing else changes.</summary>
    /// <param name="recordPath">The record's path.</param>
    /// <param name="sourcePath">The source's new path.</param>
    /// <returns>The rebound record.</returns>
    public ModelImportSettings WithSource(string recordPath, string sourcePath)
    {
        var folder = Path.GetDirectoryName(Path.GetFullPath(recordPath))!;
        var projectRoot = Path.GetDirectoryName(folder)!;
        var source = Path.GetFullPath(sourcePath);
        var relative = Path.GetRelativePath(projectRoot, source);
        return this with
        {
            Source = Path.IsPathRooted(relative) || relative.StartsWith("..", StringComparison.Ordinal)
                ? source.Replace('\\', '/')
                : Path.GetRelativePath(folder, source).Replace('\\', '/'),
        };
    }

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

    private static JsonElement EmptyProvenance()
        => JsonSerializer.SerializeToElement(new { schema_version = 2, geometries = Array.Empty<object>() });

    private static JsonSchema LoadSchema()
    {
        using var stream = typeof(ModelImportSettings).Assembly.GetManifestResourceStream("oxygen.model-import.schema.json")
            ?? throw new InvalidOperationException("The engine's model import record schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    }
}
