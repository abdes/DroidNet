// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// An authored scene camera a viewport can render through.
/// </summary>
/// <param name="NodeId">The id of the scene node that carries the camera.</param>
/// <param name="Name">The node name shown in the viewport camera menu.</param>
public sealed record SceneCameraChoice(Guid NodeId, string Name);
