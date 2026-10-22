// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of a perspective camera: its near plane stays in front of its far plane.</summary>
internal sealed class PerspectiveCameraRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneSchema.PerspectiveCamera.Kind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.PerspectiveCamera.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node?.Components.OfType<PerspectiveCamera>().FirstOrDefault();

    /// <inheritdoc/>
    public SceneDiagnostic? Validate(Scene scene, IReadOnlyList<PropertyTargetEdit> edits)
        => ComponentCandidates.Of<PerspectiveCamera>(scene, edits, this.Descriptors)
            .Where(static camera => camera.Candidate.NearPlane >= camera.Candidate.FarPlane)
            .Select(static camera => SceneDiagnostic.Error(SceneEditCodes.PropertiesInconsistent, "Near plane must be smaller than far plane.", camera.Node))
            .FirstOrDefault();
}
