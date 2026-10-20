// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of a point or spot light: the light's values stay valid together, cone angles included.</summary>
/// <param name="kind">The point or spot light kind.</param>
internal sealed class LocalLightRules(string kind) : IComponentRules
{
    /// <inheritdoc/>
    public string Kind { get; } = kind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors
        => string.Equals(this.Kind, SceneProperties.PointLightKind, StringComparison.Ordinal) ? SceneProperties.PointLight.ById : SceneProperties.SpotLight.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node)
        => string.Equals(this.Kind, SceneProperties.PointLightKind, StringComparison.Ordinal) ? node?.Components.OfType<PointLightComponent>().FirstOrDefault()
            : node?.Components.OfType<SpotLightComponent>().FirstOrDefault();

    /// <inheritdoc/>
    public SceneDiagnostic? Validate(Scene scene, IReadOnlyList<PropertyTargetEdit> edits)
        => ComponentCandidates.Of<LightComponent>(scene, edits, this.Descriptors)
            .Select(static light => (light.Node, Error: LightValidation.Validate((LightComponentData)light.Candidate.Dehydrate())))
            .Where(static light => light.Error is not null)
            .Select(static light => SceneDiagnostic.Error(SceneEditCodes.PropertiesInconsistent, light.Error!, light.Node))
            .FirstOrDefault();
}
