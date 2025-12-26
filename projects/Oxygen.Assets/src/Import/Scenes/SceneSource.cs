// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Assets.Import.Scenes;

public sealed record SceneSource(
    string Schema,
    string Name,
    List<SceneNodeSource> Nodes);

public sealed record SceneNodeSource(
    string Name,
    Vector3? Translation,
    Quaternion? Rotation,
    Vector3? Scale,
    string? Mesh,
    List<SceneNodeSource>? Children);
