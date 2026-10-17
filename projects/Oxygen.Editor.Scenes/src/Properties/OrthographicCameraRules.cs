// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of an orthographic camera: its near plane stays in front of its far plane.</summary>
internal sealed class OrthographicCameraRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneProperties.OrthographicCameraKind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.OrthographicCamera.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node?.Components.OfType<OrthographicCamera>().FirstOrDefault();

    /// <inheritdoc/>
    public SceneDiagnostic? Validate(Scene scene, IReadOnlyList<PropertyTargetEdit> edits)
        => ComponentCandidates.Of<OrthographicCamera>(scene, edits, this.Descriptors)
            .Where(static camera => camera.Candidate.NearPlane >= camera.Candidate.FarPlane)
            .Select(static camera => SceneDiagnostic.Error(SceneEditCodes.PropertiesInconsistent, "Near plane must be smaller than far plane.", camera.Node))
            .FirstOrDefault();
}
