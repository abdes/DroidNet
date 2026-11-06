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
/// The record is the model's authored file: it lives in the model's mount at the model's identity, which the file never
/// repeats, so the model <c>/Content/Models/Ship.glb</c> has the record <c>/Content/Models/Ship.glb.import.json</c>. It
/// follows the engine's <c>oxygen.model-import.schema.json</c>, which the ImportTool reads and writes too. It holds what to
/// import (the source, the settings) and what earlier imports established (the source identity, the surface provenance,
/// the node continuity, the published nodes, the source fingerprint). The source identity, a UUIDv7 the record holds, never
/// changes. <see cref="Source"/> is a file reference from the record: relative within the model's mount, the mount path of
/// a file in another mount.
/// </remarks>
public sealed record ModelImportSettings
{
    /// <summary>The record version this editor reads and writes.</summary>
    public const int CurrentSchemaVersion = 1;

    /// <summary>The version of the surface provenance a new record holds, before any import.</summary>
    public const int SurfaceProvenanceVersion = 3;

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

    /// <summary>Gets the primary source file, a file reference from the record: <c>Car.glb</c>, <c>../Media/Car.glb</c>, or
    /// <c>/RawMedia/Cars/Car.glb</c> in another mount, the same in every project that mounts its folder.</summary>
    public required string Source { get; init; }

    /// <summary>Gets the model's identity, under which its products are named (<c>/Content/Car/Car.glb</c>). The file does
    /// not hold it: it is where the file lives.</summary>
    public required AssetPath Identity { get; init; }

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

    /// <summary>Gets the files the model reads: its primary and every file its last import read.</summary>
    /// <param name="primary">The primary source file; the import's other files are relative to its folder.</param>
    /// <returns>The files.</returns>
    public IReadOnlyList<FileSystemPath> FilesRead(FileSystemPath primary)
    {
        ArgumentNullException.ThrowIfNull(primary);
        return [primary, .. (this.SourceFingerprint?.Files ?? [])
            .Select(file => FileSystemPath.From(Path.GetFullPath(Path.Combine(primary.Parent!.FullPath, file.Path))))];
    }

    /// <summary>Creates the record of a model before its first import.</summary>
    /// <param name="identity">The model's identity, which decides where the record lives.</param>
    /// <param name="source">The model's primary source file, a file reference from the record.</param>
    /// <param name="settings">The model import settings.</param>
    /// <param name="sourceIdentity">The model's source identity, a UUIDv7; a new one when omitted.</param>
    /// <returns>The record.</returns>
    /// <exception cref="ArgumentException">The source is not a file reference from the record, or the source identity is
    /// not a UUIDv7.</exception>
    public static ModelImportSettings Create(AssetPath identity, string source, JsonElement settings, Guid? sourceIdentity = null)
    {
        ArgumentNullException.ThrowIfNull(identity);
        var stable = sourceIdentity ?? Guid.CreateVersion7();
        if (stable.Version != 7)
        {
            throw new ArgumentException("A model's source identity is a UUIDv7.", nameof(sourceIdentity));
        }

        var record = new ModelImportSettings
        {
            Source = string.Empty,
            Identity = identity,
            Settings = settings.Clone(),
            SourceIdentity = stable,
            SurfaceProvenance = EmptyProvenance(),
        }.WithSource(source);
        _ = record.ToBytes();
        return record;
    }

    /// <summary>Reads and validates a record against the installed engine schema.</summary>
    /// <param name="bytes">The record's bytes.</param>
    /// <param name="identity">The model's identity, which is where the record lives.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">The bytes are not a valid model import record.</exception>
    public static ModelImportSettings Parse(ReadOnlyMemory<byte> bytes, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
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
        if (WhyNotAReference(identity, source) is { } refusal)
        {
            throw new InvalidDataException($"The model import record of {identity} names its source '{source}': {refusal}");
        }

        return new()
        {
            Source = source,
            Identity = identity,
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

    /// <summary>Finds the source of a record's bytes when the reference grammar refuses it.</summary>
    /// <param name="bytes">The bytes of a record that could not be read.</param>
    /// <param name="identity">The model's identity.</param>
    /// <returns>The source as written and the refusal's code, or <see langword="null"/> when the record was refused for
    /// another reason.</returns>
    public static (string Source, string Code)? FindRefusedSource(ReadOnlyMemory<byte> bytes, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        try
        {
            using var document = JsonDocument.Parse(bytes);
            return document.RootElement.ValueKind == JsonValueKind.Object
                && document.RootElement.TryGetProperty("source", out var source) && source.ValueKind == JsonValueKind.String
                && !AssetPath.TryResolveReference(identity, source.GetString()!, VirtualPathRuleSet.Cooked, out _, out var error)
                    ? (source.GetString()!, error.Value.ToCode())
                    : null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    /// <summary>Gets the record's source file, through the project's mount table.</summary>
    /// <param name="project">The project and its mount table.</param>
    /// <returns>The source file, or <see langword="null"/> when its mount is not bound on this machine.</returns>
    public FileSystemPath? ResolveSource(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return project.LocateReference(this.Identity, this.Source);
    }

    /// <summary>Gets the record with its source rewritten, as relocating the model does; nothing else changes.</summary>
    /// <param name="source">The source's new place, a file reference from the record.</param>
    /// <returns>The rebound record.</returns>
    /// <exception cref="ArgumentException">The source is not a file reference from the record.</exception>
    public ModelImportSettings WithSource(string source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return WhyNotAReference(this.Identity, source) is { } refusal
            ? throw new ArgumentException($"The model import record of {this.Identity} cannot name its source '{source}': {refusal}", nameof(source))
            : this with { Source = source };
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
    /// with <paramref name="continues"/>, and takes its ID on the next import. Entries in a circle block the model, so an
    /// entry that would close one replaces the entry it closes; nothing else changes.</summary>
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

        var updated = this with { NodeContinuity = this.WithoutEntryClosingCircle(anchor, continues).Add(new ModelNodeContinuity(anchor, continues)) };
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

    // Why a text is not a file reference from the record, by the engine's grammar; null when it is.
    private static string? WhyNotAReference(AssetPath identity, string source)
        => AssetPath.TryResolveReference(identity, source, VirtualPathRuleSet.Cooked, out _, out var error) ? null : error.Value.ToMessage();

    private static JsonElement EmptyProvenance()
        => JsonSerializer.SerializeToElement(new { schema_version = SurfaceProvenanceVersion, geometries = Array.Empty<object>() });

    // The entries left once the chain that starts at the continued node no longer leads back to the continuing node.
    private ImmutableArray<ModelNodeContinuity> WithoutEntryClosingCircle(string anchor, string continues)
    {
        var next = this.NodeContinuity.ToDictionary(static entry => entry.Anchor, StringComparer.Ordinal);
        var visited = new HashSet<string>(StringComparer.Ordinal);
        for (var current = continues; visited.Add(current) && next.TryGetValue(current, out var entry); current = entry.Continues)
        {
            if (string.Equals(entry.Continues, anchor, StringComparison.Ordinal))
            {
                return this.NodeContinuity.Remove(entry);
            }
        }

        return this.NodeContinuity;
    }
}
