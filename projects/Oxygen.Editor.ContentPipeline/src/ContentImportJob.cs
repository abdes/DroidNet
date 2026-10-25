// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One native import manifest job.
/// </summary>
/// <remarks>
/// A script sidecar job has no source: its bindings are inline, and it is attributed to its scene through
/// <see cref="OwnerSource"/>.
/// </remarks>
public sealed record ContentImportJob(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("type")] string Type,
    [property: JsonIgnore] string? Source,
    [property: JsonPropertyName("depends_on")] IReadOnlyList<string> DependsOn,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    [property: JsonPropertyName("output")] string? Output,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    [property: JsonPropertyName("name")] string? Name)
{
    /// <summary>Gets the explicit texture output identity shared by analysis and cooking.</summary>
    [JsonPropertyName("virtual_path")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? VirtualPath { get; init; }

    /// <summary>Gets the model import record a <c>model</c> job runs, relative to the folder sources resolve from.</summary>
    /// <remarks>The record holds the model's source, virtual path, settings and identity; the job carries none of them.</remarks>
    [JsonPropertyName("record")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Record { get; init; }

    /// <summary>Gets the type the engine reports for this job: a <c>model</c> job runs, and reports, as the <c>gltf</c> or
    /// <c>fbx</c> job its source's format makes it.</summary>
    [JsonIgnore]
    public string ReportedType => this.Record is null
        ? this.Type
        : string.Equals(Path.GetExtension(this.Source), ".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf";

    /// <summary>Gets the source the manifest names: none for a <c>model</c> job, whose record names it.</summary>
    [JsonPropertyName("source")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ManifestSource => this.Record is null ? this.Source : null;

    /// <summary>Gets per-job descriptor folders while retaining the manifest's shared cooked root.</summary>
    [JsonPropertyName("layout")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public ContentImportLayout? Layout { get; init; }

    /// <summary>Gets the ordered resolver-only roots for scene and geometry descriptors.</summary>
    [JsonPropertyName("cooked_context_roots")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyList<string>? CookedContextRoots { get; init; }

    /// <summary>Gets whether a script job compiles its source to bytecode.</summary>
    [JsonPropertyName("compile")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? Compile { get; init; }

    /// <summary>Gets the script compile mode, <c>debug</c> or <c>optimized</c>.</summary>
    [JsonPropertyName("compile_mode")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? CompileMode { get; init; }

    /// <summary>Gets the script storage, <c>embedded</c> or <c>external</c>.</summary>
    [JsonPropertyName("script_storage")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ScriptStorage { get; init; }

    /// <summary>Gets the authoring root that an external script's recorded source path is relative to.</summary>
    [JsonPropertyName("source_root")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? SourceRoot { get; init; }

    /// <summary>Gets the scene a script sidecar attaches its slots to.</summary>
    [JsonPropertyName("target_scene_virtual_path")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? TargetSceneVirtualPath { get; init; }

    /// <summary>Gets a script sidecar's slot bindings.</summary>
    [JsonPropertyName("bindings")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyList<ScriptSidecarBinding>? Bindings { get; init; }

    /// <summary>Gets a script sidecar's script parameter changes of the scene's instances.</summary>
    [JsonPropertyName("instance_bindings")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyList<ScriptSidecarInstanceBinding>? InstanceBindings { get; init; }

    /// <summary>Gets the source of the job that owns this one's output, for a job without its own source.</summary>
    [JsonIgnore]
    public string? OwnerSource { get; init; }

    /// <summary>Gets the source this job is attributed to: its own, or its owner's.</summary>
    [JsonIgnore]
    public string AttributedSource => this.Source ?? this.OwnerSource ?? string.Empty;
}
