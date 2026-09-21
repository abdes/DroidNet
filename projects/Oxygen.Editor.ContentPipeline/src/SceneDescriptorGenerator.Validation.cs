// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Json.Schema;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Validates authored post-process values before editing or cooking.</summary>
public sealed partial class SceneDescriptorGenerator
{
    private static readonly Lazy<JsonSchema> PostProcessSchema = new(() =>
    {
        var sceneSchema = SceneSchemas.Value.GetEngineSchema("oxygen.scene-descriptor.schema.json");
        var schema = sceneSchema["definitions"]!["post_process_volume_environment"]!.DeepClone().AsObject();
        schema["$schema"] = sceneSchema["$schema"]!.DeepClone();
        schema["definitions"] = sceneSchema["definitions"]!.DeepClone();
        return JsonSchema.FromText(schema.ToJsonString());
    });

    /// <summary>Checks the native schema and relationships without changing authored values.</summary>
    /// <param name="authored">The complete candidate post-process state.</param>
    /// <returns>A readable rejection reason, or <see langword="null"/> when valid.</returns>
    /// <remarks>Camera-dependent and resolved gain checks remain owned by the native exposure resolver.</remarks>
    public static string? ValidatePostProcess(PostProcessEnvironmentData authored)
    {
        ArgumentNullException.ThrowIfNull(authored);
        if (ValidateExposureRelationships(authored) is { } issue)
        {
            return issue;
        }

        JsonElement instance;
        try
        {
            instance = JsonSerializer.SerializeToElement(CreatePostProcess(authored), SceneDescriptorJson.Options);
        }
        catch (ArgumentException)
        {
            return "Post-process values and curve keys must be finite numbers.";
        }

        var evaluation = PostProcessSchema.Value.Evaluate(instance, new EvaluationOptions { OutputFormat = OutputFormat.List });
        if (!evaluation.IsValid)
        {
            var rejectedField = evaluation.Details?
                .FirstOrDefault(static detail => !detail.IsValid)?.InstanceLocation.ToString();
            return string.IsNullOrEmpty(rejectedField)
                ? "Post-process values must satisfy the native scene schema."
                : $"Post-process field '{rejectedField}' is outside its allowed range or has an invalid value.";
        }

        return float.IsFinite(1f / authored.AutoExposureLogLuminanceRange)
            ? null
            : "Auto exposure histogram range is too small to represent its inverse.";
    }
}
