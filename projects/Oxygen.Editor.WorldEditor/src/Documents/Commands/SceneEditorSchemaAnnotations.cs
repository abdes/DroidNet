// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Loads editor annotations from <c>oxygen.scene-descriptor.editor.schema.json</c>.
/// </summary>
internal static class SceneEditorSchemaAnnotations
{
    /// <summary>
    /// Gets the engine schema file that the scene editor overlay extends.
    /// </summary>
    internal const string EngineSchemaFileName = "oxygen.scene-descriptor.schema.json";

    private static readonly Lazy<IReadOnlyDictionary<string, EditorAnnotation>> LazyAnnotations = new(LoadAnnotations);

    /// <summary>
    /// Gets an overlay-backed annotation and fills any intentionally omitted
    /// fields from the supplied descriptor default.
    /// </summary>
    /// <param name="schemaPointer">The overlay JSON pointer that owns the editor annotation.</param>
    /// <param name="fallback">The descriptor-local fallback used only for non-visible metadata.</param>
    /// <returns>The merged editor annotation.</returns>
    internal static EditorAnnotation Get(string schemaPointer, EditorAnnotation fallback)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(schemaPointer);
        ArgumentNullException.ThrowIfNull(fallback);

        if (!LazyAnnotations.Value.TryGetValue(schemaPointer, out var annotation))
        {
            throw new KeyNotFoundException($"Scene editor overlay is missing annotation for {schemaPointer}.");
        }

        return Merge(annotation, fallback);
    }

    private static IReadOnlyDictionary<string, EditorAnnotation> LoadAnnotations()
    {
        var catalog = EditorSchemaCatalog.LoadFromDirectory(LocateSchemasDirectory());
        var overlay = catalog.TryGetOverlay(EngineSchemaFileName)
            ?? throw new InvalidOperationException($"Scene editor overlay not found for {EngineSchemaFileName}.");
        return EditorSchemaOverlay.ExtractAnnotations(overlay);
    }

    private static EditorAnnotation Merge(EditorAnnotation annotation, EditorAnnotation fallback)
        => new()
        {
            Label = annotation.Label ?? fallback.Label,
            Group = annotation.Group ?? fallback.Group,
            Order = annotation.Order ?? fallback.Order,
            Renderer = annotation.Renderer ?? fallback.Renderer,
            Tooltip = annotation.Tooltip ?? fallback.Tooltip,
            Step = annotation.Step ?? fallback.Step,
            SoftMax = annotation.SoftMax ?? fallback.SoftMax,
            SoftMin = annotation.SoftMin ?? fallback.SoftMin,
            Advanced = annotation.Advanced || fallback.Advanced,
            Extra = annotation.Extra.Count == 0 ? fallback.Extra : annotation.Extra,
        };

    private static string LocateSchemasDirectory()
    {
        var schemasAssemblyDir = Path.GetDirectoryName(typeof(EditorSchemaCatalog).Assembly.Location)
            ?? AppContext.BaseDirectory;
        var primary = Path.Combine(schemasAssemblyDir, "Schemas");
        if (Directory.Exists(primary))
        {
            return primary;
        }

        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "Schemas");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException($"Could not find Schemas/ next to {schemasAssemblyDir} or {AppContext.BaseDirectory}.");
    }
}
