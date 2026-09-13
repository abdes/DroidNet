// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Identifies a scene activation in one engine run and one document lifetime.</summary>
/// <param name="RunId">The runtime run that owns the scene.</param>
/// <param name="SceneId">The authored scene identity.</param>
/// <param name="DocumentLifetime">The identity of the open document instance.</param>
/// <param name="ActivationId">The identity of this projection, invalidated by scene replacement.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeSceneTarget(Guid RunId, Guid SceneId, Guid DocumentLifetime, Guid ActivationId)
{
    /// <summary>Gets the owning project identity for cross-workspace status isolation.</summary>
    public Guid ProjectId { get; init; }
}
