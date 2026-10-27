// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>
/// The files of the project's authored scenes, which an edit reads and writes while it validates and applies, on the
/// scene thread: <i>Apply to source</i> changes a scene that is not open (scene instances 6.7).
/// </summary>
public interface IAuthoredSceneFiles
{
    /// <summary>Reads a scene's file as it is now.</summary>
    /// <param name="scene">The scene's asset, such as <c>/Content/Prefabs/Watchtower.oscene</c>.</param>
    /// <returns>The file's bytes, or <see langword="null"/> when the asset is not a readable scene file of the project.</returns>
    public byte[]? Read(AssetPath scene);

    /// <summary>Replaces a scene's file.</summary>
    /// <param name="scene">The scene's asset.</param>
    /// <param name="content">The file's new bytes.</param>
    /// <exception cref="IOException">The file could not be written.</exception>
    public void Write(AssetPath scene, byte[] content);
}
