// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using Json.Schema;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Loads engine schemas and editor overlays from a directory and merges
/// them by JSON Pointer to drive descriptor generation.
/// </summary>
public sealed class EditorSchemaCatalog
{
    private static readonly EvaluationOptions DefaultEvaluationOptions = new()
    {
        OutputFormat = OutputFormat.List,
    };

    private readonly Dictionary<string, JsonObject> engineByFileName;
    private readonly Dictionary<string, JsonObject> overlayByEngineFileName;
    private readonly Dictionary<string, JsonSchema> builtEngineSchemas = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, JsonSchema> builtMergedSchemas = new(StringComparer.OrdinalIgnoreCase);
    private readonly object buildGate = new();

    private EditorSchemaCatalog(
        Dictionary<string, JsonObject> engineByFileName,
        Dictionary<string, JsonObject> overlayByEngineFileName)
    {
        this.engineByFileName = engineByFileName;
        this.overlayByEngineFileName = overlayByEngineFileName;
    }

    /// <summary>
    /// Gets all engine schema file names (e.g. <c>oxygen.transform-component.schema.json</c>).
    /// </summary>
    public IReadOnlyCollection<string> EngineSchemas => this.engineByFileName.Keys;

    /// <summary>
    /// Loads schemas from a directory.
    /// </summary>
    /// <param name="directory">The directory containing both engine
    /// schemas (<c>*.schema.json</c>) and editor overlays
    /// (<c>*.editor.schema.json</c>).</param>
    /// <returns>The new catalog.</returns>
    public static EditorSchemaCatalog LoadFromDirectory(string directory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);
        if (!Directory.Exists(directory))
        {
            throw new DirectoryNotFoundException($"Schema directory not found: {directory}");
        }

        var engineFiles = Directory.GetFiles(directory, "*.schema.json", SearchOption.TopDirectoryOnly)
            .Where(static path => !path.EndsWith(".editor.schema.json", StringComparison.OrdinalIgnoreCase))
            .ToList();
        var overlayFiles = Directory.GetFiles(directory, "*.editor.schema.json", SearchOption.TopDirectoryOnly).ToList();

        var engineByFileName = new Dictionary<string, JsonObject>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in engineFiles)
        {
            engineByFileName[Path.GetFileName(path)] = LoadJsonObject(path);
        }

        var overlayByEngineFileName = new Dictionary<string, JsonObject>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in overlayFiles)
        {
            var fileName = Path.GetFileName(path);
            // Convention: foo.editor.schema.json overlays foo.schema.json
            var engineName = fileName.Replace(".editor.schema.json", ".schema.json", StringComparison.OrdinalIgnoreCase);
            overlayByEngineFileName[engineName] = LoadJsonObject(path);
        }

        return new EditorSchemaCatalog(engineByFileName, overlayByEngineFileName);
    }

    /// <summary>
    /// Returns the engine schema for the given file name.
    /// </summary>
    /// <param name="engineFileName">The engine schema file name.</param>
    /// <returns>The schema as a <see cref="JsonObject"/>.</returns>
    public JsonObject GetEngineSchema(string engineFileName)
    {
        if (!this.engineByFileName.TryGetValue(engineFileName, out var schema))
        {
            throw new KeyNotFoundException($"Engine schema not found: {engineFileName}");
        }

        return schema;
    }

    /// <summary>
    /// Returns the overlay schema for the given engine schema, when present.
    /// </summary>
    /// <param name="engineFileName">The engine schema file name.</param>
    /// <returns>The overlay, or <see langword="null"/> when no overlay is
    /// authored for this engine schema.</returns>
    public JsonObject? TryGetOverlay(string engineFileName)
    {
        return this.overlayByEngineFileName.TryGetValue(engineFileName, out var overlay) ? overlay : null;
    }

    /// <summary>
    /// Validates an instance against the engine schema for the given file name.
    /// </summary>
    /// <param name="engineFileName">The engine schema file name.</param>
    /// <param name="instance">The instance JSON.</param>
    /// <returns><see langword="true"/> when the instance is valid.</returns>
    public bool ValidateAgainstEngine(string engineFileName, JsonNode? instance)
    {
        var schema = this.GetOrBuildEngineSchema(engineFileName);
        return schema.Evaluate(ToElement(instance), DefaultEvaluationOptions).IsValid;
    }

    /// <summary>
    /// Validates an instance against the merged engine + overlay schema.
    /// Returns the same accept/reject as the engine alone (overlays must
    /// not introduce constraints; this is enforced by CI lint #3).
    /// </summary>
    /// <param name="engineFileName">The engine schema file name.</param>
    /// <param name="instance">The instance JSON.</param>
    /// <returns><see langword="true"/> when the instance is valid.</returns>
    public bool ValidateAgainstMerged(string engineFileName, JsonNode? instance)
    {
        var schema = this.GetOrBuildMergedSchema(engineFileName);
        var options = new EvaluationOptions
        {
            OutputFormat = OutputFormat.List,
        };
        return schema.Evaluate(ToElement(instance), options).IsValid;
    }

    private static JsonElement ToElement(JsonNode? node)
        => node is null ? default : JsonSerializer.SerializeToElement(node);

    private static JsonObject LoadJsonObject(string path)
    {
        using var stream = File.OpenRead(path);
        var node = JsonNode.Parse(stream, new JsonNodeOptions { PropertyNameCaseInsensitive = false }, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip });
        if (node is not JsonObject obj)
        {
            throw new InvalidDataException($"Schema file is not a JSON object: {path}");
        }

        return obj;
    }

    private JsonSchema GetOrBuildEngineSchema(string engineFileName)
    {
        lock (this.buildGate)
        {
            if (this.builtEngineSchemas.TryGetValue(engineFileName, out var schema))
            {
                return schema;
            }

            var schemaJson = CloneForValidation(this.GetEngineSchema(engineFileName));
            schema = JsonSchema.FromText(schemaJson.ToJsonString());
            this.builtEngineSchemas[engineFileName] = schema;
            return schema;
        }
    }

    private JsonSchema GetOrBuildMergedSchema(string engineFileName)
    {
        lock (this.buildGate)
        {
            if (this.builtMergedSchemas.TryGetValue(engineFileName, out var schema))
            {
                return schema;
            }

            var overlay = this.TryGetOverlay(engineFileName);
            if (overlay is null)
            {
                schema = this.GetOrBuildEngineSchema(engineFileName);
                this.builtMergedSchemas[engineFileName] = schema;
                return schema;
            }

            var resolver = new InMemorySchemaResolver(this.GetOrBuildEngineSchema);
            var buildOptions = new BuildOptions();
            buildOptions.SchemaRegistry.Fetch = resolver.Resolve;

            schema = JsonSchema.FromText(CloneForValidation(overlay).ToJsonString(), buildOptions);
            this.builtMergedSchemas[engineFileName] = schema;
            return schema;
        }
    }

    private static JsonObject CloneForValidation(JsonObject schema)
    {
        var clone = schema.DeepClone().AsObject();
        _ = clone.Remove("$id");
        return clone;
    }

    private sealed class InMemorySchemaResolver
    {
        private readonly Func<string, JsonSchema> resolveByFileName;

        public InMemorySchemaResolver(Func<string, JsonSchema> resolveByFileName)
        {
            this.resolveByFileName = resolveByFileName;
        }

        public IBaseDocument? Resolve(Uri uri, SchemaRegistry _)
        {
            var fileName = Path.GetFileName(uri.LocalPath);
            if (string.IsNullOrEmpty(fileName))
            {
                fileName = Path.GetFileName(uri.OriginalString);
            }

            if (!string.IsNullOrEmpty(fileName))
            {
                try
                {
                    return this.resolveByFileName(fileName);
                }
                catch (KeyNotFoundException)
                {
                    return null;
                }
            }

            return null;
        }
    }
}
