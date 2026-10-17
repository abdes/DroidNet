// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>Every scene property: its component kind, its descriptor and its kind's rules.</summary>
public static class SceneProperties
{
    /// <summary>The component kind of a node's transform.</summary>
    public const string TransformKind = "transform";

    /// <summary>The component kind of a node's geometry.</summary>
    public const string GeometryKind = "geometry";

    /// <summary>The component kind of a perspective camera.</summary>
    public const string PerspectiveCameraKind = "perspective-camera";

    /// <summary>The component kind of an orthographic camera.</summary>
    public const string OrthographicCameraKind = "orthographic-camera";

    /// <summary>The component kind of a directional light.</summary>
    public const string DirectionalLightKind = "directional-light";

    /// <summary>The component kind of a point light.</summary>
    public const string PointLightKind = "point-light";

    /// <summary>The component kind of a spot light.</summary>
    public const string SpotLightKind = "spot-light";

    /// <summary>The component kind of a local fog volume.</summary>
    public const string LocalFogVolumeKind = "local-fog-volume";

    /// <summary>The kind of a node's authored rendering flags.</summary>
    public const string NodeRenderingKind = "node-rendering";

    /// <summary>The kind of the scene's environment.</summary>
    public const string SceneEnvironmentKind = "scene-environment";

    private static readonly FrozenDictionary<string, IComponentRules> Rules = new IComponentRules[]
    {
        new TransformRules(),
        new PerspectiveCameraRules(),
        new OrthographicCameraRules(),
        new DirectionalLightRules(),
        new LocalLightRules(PointLightKind),
        new LocalLightRules(SpotLightKind),
        new LocalFogVolumeRules(),
        new NodeRenderingRules(),
        new SceneEnvironmentRules(),
    }.ToFrozenDictionary(static rules => rules.Kind, StringComparer.Ordinal);

    /// <summary>Gets the transform descriptors.</summary>
    public static TransformDescriptors Transform { get; } = TransformDescriptors.Build();

    /// <summary>Gets the geometry descriptors.</summary>
    /// <remarks>A geometry is set with its own edit, which checks the asset; it has no property rules.</remarks>
    public static GeometryDescriptors Geometry { get; } = GeometryDescriptors.Build();

    /// <summary>Gets the perspective camera descriptors.</summary>
    public static PerspectiveCameraDescriptors PerspectiveCamera { get; } = PerspectiveCameraDescriptors.Build();

    /// <summary>Gets the orthographic camera descriptors.</summary>
    public static OrthographicCameraDescriptors OrthographicCamera { get; } = OrthographicCameraDescriptors.Build();

    /// <summary>Gets the directional light descriptors.</summary>
    public static DirectionalLightDescriptors DirectionalLight { get; } = DirectionalLightDescriptors.Build();

    /// <summary>Gets the point light descriptors.</summary>
    public static LocalLightDescriptors PointLight { get; } = LocalLightDescriptors.BuildPoint();

    /// <summary>Gets the spot light descriptors.</summary>
    public static LocalLightDescriptors SpotLight { get; } = LocalLightDescriptors.BuildSpot();

    /// <summary>Gets the local fog volume descriptors.</summary>
    public static LocalFogVolumeDescriptors LocalFogVolume { get; } = LocalFogVolumeDescriptors.Build();

    /// <summary>Gets the node rendering flag descriptors.</summary>
    public static NodeRenderingDescriptors NodeRendering { get; } = NodeRenderingDescriptors.Build();

    /// <summary>Gets the scene environment descriptors.</summary>
    public static SceneEnvironmentDescriptors SceneEnvironment { get; } = SceneEnvironmentDescriptors.Build();

    /// <summary>Gets the rules of a component kind.</summary>
    /// <param name="kind">The component kind.</param>
    /// <returns>The rules, or <see langword="null"/> when the kind's properties are not edited through property edits.</returns>
    public static IComponentRules? RulesOf(string kind) => Rules.GetValueOrDefault(kind);

    /// <summary>Gets the rules of the one component kind an edit's properties belong to.</summary>
    /// <param name="edits">The edits.</param>
    /// <returns>The rules, or <see langword="null"/> when the edits span several kinds, none, or one without rules.</returns>
    public static IComponentRules? RulesOf(IEnumerable<PropertyEdit> edits) => RulesOf(edits.SelectMany(static edit => edit.Ids));

    /// <summary>Gets the rules of the one component kind properties belong to.</summary>
    /// <param name="properties">The properties.</param>
    /// <returns>The rules, or <see langword="null"/> when the properties span several kinds, none, or one without rules.</returns>
    public static IComponentRules? RulesOf(IEnumerable<PropertyId> properties)
    {
        string[] kinds = [.. properties.Select(static id => id.ComponentKind).Distinct(StringComparer.Ordinal)];
        return kinds.Length == 1 ? RulesOf(kinds[0]) : null;
    }

    /// <summary>Gets the rules of every component kind that has them.</summary>
    /// <returns>The rules.</returns>
    public static IEnumerable<IComponentRules> All() => Rules.Values;
}
