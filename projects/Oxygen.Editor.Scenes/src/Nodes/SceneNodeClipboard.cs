// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>The project session's clipboard of scene nodes.</summary>
/// <remarks>
/// The scene a cut comes from is held weakly: a cut never keeps a closed scene alive, and a scene that reloads as
/// another instance ends the cut.
/// </remarks>
public sealed class SceneNodeClipboard : ISceneNodeClipboard
{
    private WeakReference<Scene>? origin;

    /// <inheritdoc/>
    public SceneClipboard? Payload { get; private set; }

    /// <inheritdoc/>
    public bool IsCut { get; private set; }

    /// <inheritdoc/>
    public int Generation { get; private set; }

    /// <inheritdoc/>
    public void Stage(SceneClipboard payload, bool cut, Scene origin)
    {
        ArgumentNullException.ThrowIfNull(payload);
        ArgumentNullException.ThrowIfNull(origin);
        this.Payload = payload;
        this.IsCut = cut;
        this.origin = new WeakReference<Scene>(origin);
        this.Generation++;
    }

    /// <inheritdoc/>
    public bool IsCurrentFor(Scene? scene)
        => !this.IsCut
            || (scene is not null && this.origin is { } staged && staged.TryGetTarget(out var source) && ReferenceEquals(source, scene));

    /// <inheritdoc/>
    public void Clear()
    {
        this.Payload = null;
        this.IsCut = false;
        this.origin = null;
        this.Generation++;
    }
}
