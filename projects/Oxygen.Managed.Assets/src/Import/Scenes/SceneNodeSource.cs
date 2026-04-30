// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record SceneNodeSource(
    string Name,
    Vector3? Translation,
    Quaternion? Rotation,
    Vector3? Scale,
    string? Mesh,
    IReadOnlyList<SceneNodeSource>? Children)
{
    public Guid? Id { get; init; }

    public SceneNodeFlagsSource? Flags { get; init; }

    public PerspectiveCameraSource? PerspectiveCamera { get; init; }

    public DirectionalLightSource? DirectionalLight { get; init; }

    public PointLightSource? PointLight { get; init; }

    public SpotLightSource? SpotLight { get; init; }
}
