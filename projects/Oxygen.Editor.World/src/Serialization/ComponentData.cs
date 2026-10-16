// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Base data transfer object for game components.
/// </summary>
[JsonPolymorphic(TypeDiscriminatorPropertyName = "$type")]
[JsonDerivedType(typeof(TransformData), "Transform")]
[JsonDerivedType(typeof(GeometryComponentData), "GeometryComponent")]
[JsonDerivedType(typeof(PerspectiveCameraData), "PerspectiveCamera")]
[JsonDerivedType(typeof(OrthographicCameraData), "OrthographicCamera")]
[JsonDerivedType(typeof(DirectionalLightData), "DirectionalLight")]
[JsonDerivedType(typeof(PointLightData), "PointLight")]
[JsonDerivedType(typeof(SpotLightData), "SpotLight")]
[JsonDerivedType(typeof(LocalFogVolumeData), "LocalFogVolume")]
[JsonDerivedType(typeof(ScriptingData), "Scripting")]
public abstract record ComponentData : NamedData
{
    /// <summary>
    /// Gets the stable component identity.
    /// </summary>
    public ComponentId Id { get; init; } = ComponentId.New();
}
