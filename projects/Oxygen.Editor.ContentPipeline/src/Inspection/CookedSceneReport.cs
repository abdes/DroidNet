// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Collections.Immutable;
using System.Globalization;
using System.Text.Json;
using Json.Schema;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>The schema-validated composed scenes of a cooked root, as the Inspector's <c>scenes</c> verb reports them.</summary>
public sealed class CookedSceneReport
{
    private static readonly Lazy<JsonSchema> Schema = new(static () => CookedReportSchemas.Load("oxygen.cooked-scenes.schema.json"));

    // The editor's identity of each changeable property, by component kind, whether it is a part's, and key.
    private static readonly FrozenDictionary<(string Component, bool IsPart, string Key), PropertyId> Changeable
        = SceneSchema.Changeable.Keys.ToFrozenDictionary(static id => (id.ComponentKind, id.PartKind is not null, id.JsonPointer));

    private readonly JsonElement scenes;

    private CookedSceneReport(JsonElement scenes) => this.scenes = scenes;

    /// <summary>Gets the number of scenes the report composes.</summary>
    public int Count => this.scenes.GetArrayLength();

    /// <summary>Validates a report against the shared native schema.</summary>
    /// <param name="json">The native report.</param>
    /// <returns>The report.</returns>
    /// <exception cref="InvalidDataException">The report does not match its schema.</exception>
    public static CookedSceneReport Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        return Schema.Value.Evaluate(document.RootElement).IsValid
            ? new(document.RootElement.GetProperty("scenes").Clone())
            : throw new InvalidDataException("The native scene report does not match its schema.");
    }

    /// <summary>Reads the structure of the one scene a report scoped to one virtual path composes.</summary>
    /// <param name="source">The source the scene is the instanced scene of.</param>
    /// <returns>The structure, or null when the root has no such scene.</returns>
    /// <exception cref="InvalidDataException">The report holds several scenes, the engine could not compose the scene,
    /// or the report contradicts itself.</exception>
    public SceneStructure? SingleOrDefault(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return this.Count switch
        {
            0 => null,
            1 => ReadScene(source, this.scenes[0]),
            _ => throw new InvalidDataException("The native scene query returned more than one scene."),
        };
    }

    private static SceneStructure ReadScene(AssetPath source, JsonElement scene)
    {
        if (!scene.GetProperty("complete").GetBoolean())
        {
            throw new InvalidDataException($"The engine could not compose the scene: {scene.GetProperty("diagnostic").GetString()}");
        }

        var occurrences = scene.GetProperty("occurrences").EnumerateArray()
            .Select(static occurrence => new StructureScene(occurrence.GetProperty("scene").GetGuid(), occurrence.GetProperty("virtual_path").GetString()))
            .ToImmutableArray();
        var rows = scene.GetProperty("nodes").EnumerateArray().ToImmutableArray();
        var paths = rows.Select(PathOf).ToImmutableArray();
        var context = new Context(occurrences, paths);
        var nodes = rows.Select((row, position) => ReadNode(row, paths[position], context)).ToImmutableArray();
        var unresolved = scene.GetProperty("unresolved").EnumerateArray().Select(change => ReadUnresolved(change, context)).ToImmutableArray();
        var issues = scene.GetProperty("issues").EnumerateArray().Select(issue => new StructureIssue(
            issue.GetProperty("code").GetString()!,
            context.Occurrence(issue.GetProperty("occurrence")),
            issue.GetProperty("node").ValueKind == JsonValueKind.Null ? null : context.Node(issue.GetProperty("node")),
            issue.GetProperty("scene").ValueKind == JsonValueKind.Null ? null : issue.GetProperty("scene").GetGuid())).ToImmutableArray();
        return new(source, occurrences[0], nodes, unresolved, issues);
    }

    // A node of the source is its ID alone; a node of a nested instance is the source's instance root, then its path.
    private static NodePath PathOf(JsonElement row)
    {
        var below = row.GetProperty("node_path").EnumerateArray().Select(static id => NodeId.FromValue(id.GetGuid())).ToArray();
        var node = NodeId.FromValue(row.GetProperty("node_id").GetGuid());
        if (below.Length == 0)
        {
            return NodePath.Of(node);
        }

        return below[^1] == node
            ? NodePath.Of([NodeId.FromValue(row.GetProperty("root").GetGuid()), .. below])
            : throw new InvalidDataException($"The composed node {node} has a node path that ends elsewhere.");
    }

    private static StructureNode ReadNode(JsonElement row, NodePath path, Context context)
    {
        var components = ImmutableArray.CreateBuilder<string>();
        var properties = ImmutableArray.CreateBuilder<StructureProperty>();
        foreach (var component in row.GetProperty("components").EnumerateArray())
        {
            var kind = component.GetProperty("kind").GetString()!;
            components.Add(kind);
            properties.AddRange(component.GetProperty("properties").EnumerateArray().Select(property => new StructureProperty(
                PropertyOf(kind, isPart: false, property), Part: null, property.GetProperty("value").Clone(), context.Occurrence(property.GetProperty("supplier")))));
        }

        Guid? geometry = null;
        if (row.TryGetProperty("geometry", out var geometryRow))
        {
            components.Add(SceneSchema.Geometry.Kind);
            geometry = geometryRow.GetProperty("asset_key").GetGuid();
            properties.AddRange(geometryRow.GetProperty("overrides").EnumerateArray().Select(context.GeometryProperty));
        }

        var scripts = row.GetProperty("scripts").EnumerateArray().Select(context.Script).ToImmutableArray();
        if (!scripts.IsEmpty)
        {
            components.Add(SceneSchema.Scripting.Kind);
        }

        StructureScene? instance = null;
        if (row.TryGetProperty("instance", out var instanceRow))
        {
            components.Add(SceneSchema.SceneInstance.Kind);
            instance = context.SceneOf(instanceRow.GetProperty("source").GetGuid());
        }

        var parent = row.GetProperty("parent");
        return new(
            path,
            row.GetProperty("name").GetString()!,
            parent.ValueKind == JsonValueKind.Null ? null : context.Node(parent),
            context.Occurrence(row.GetProperty("occurrence")),
            components.ToImmutable(),
            properties.ToImmutable(),
            geometry,
            scripts,
            instance);
    }

    private static StructureUnresolvedChange ReadUnresolved(JsonElement change, Context context)
    {
        UnresolvedProperty? property = null;
        UnresolvedParameter? parameter = null;
        if (change.TryGetProperty("property", out var propertyRow))
        {
            var part = propertyRow.GetProperty("part_id");
            property = new(
                propertyRow.GetProperty("override_id").GetInt32(),
                propertyRow.GetProperty("key").GetString(),
                part.ValueKind == JsonValueKind.Null ? null : part.GetGuid());
        }
        else
        {
            var parameterRow = change.GetProperty("parameter");
            parameter = new(parameterRow.GetProperty("slot_id").GetGuid(), parameterRow.GetProperty("key").GetString()!);
        }

        return new(
            context.Occurrence(change.GetProperty("supplier")),
            context.Node(change.GetProperty("instance_root")),
            NodePath.Of(change.GetProperty("node_path").EnumerateArray().Select(static id => NodeId.FromValue(id.GetGuid()))),
            property,
            parameter);
    }

    private static PropertyId PropertyOf(string component, bool isPart, JsonElement property)
    {
        var key = property.GetProperty("key").GetString()!;
        return Changeable.TryGetValue((component, isPart, key), out var id)
            ? id
            : throw new InvalidDataException($"The composed scene changes {component} property '{key}', which the editor's scene schema cannot change.");
    }

    // The scenes and node paths a scene's rows refer to by position, checked as they are read.
    private sealed class Context(ImmutableArray<StructureScene> occurrences, ImmutableArray<NodePath> paths)
    {
        public StructureScene Occurrence(JsonElement position)
            => position.GetInt32() is var index && index < occurrences.Length
                ? occurrences[index]
                : throw new InvalidDataException(string.Create(CultureInfo.InvariantCulture, $"The composed scene refers to scene occurrence {index}, which it does not have."));

        public NodePath Node(JsonElement position)
            => position.GetInt32() is var index && index < paths.Length
                ? paths[index]
                : throw new InvalidDataException(string.Create(CultureInfo.InvariantCulture, $"The composed scene refers to node {index}, which it does not have."));

        // A nested source's virtual path is the one its occurrence reports, when its root holds it.
        public StructureScene SceneOf(Guid key)
            => occurrences.FirstOrDefault(scene => scene.Key == key) ?? new(key, VirtualPath: null);

        public StructureProperty GeometryProperty(JsonElement row)
        {
            var part = row.GetProperty("part_id");
            var id = PropertyOf(SceneSchema.Geometry.Kind, part.ValueKind != JsonValueKind.Null, row);
            return new(
                id,
                id.PartKind is { } kind ? new PartId(kind, part.GetGuid()) : null,
                row.GetProperty("value").Clone(),
                this.Occurrence(row.GetProperty("supplier")));
        }

        public StructureScript Script(JsonElement row) => new(
            row.GetProperty("slot_id").GetGuid(),
            row.GetProperty("script").GetGuid(),
            row.GetProperty("execution_order").GetInt32(),
            [.. row.GetProperty("parameters").EnumerateArray().Select(parameter => new StructureParameter(
                parameter.GetProperty("key").GetString()!,
                parameter.GetProperty("value").Clone(),
                this.Occurrence(parameter.GetProperty("supplier"))))]);
    }
}
