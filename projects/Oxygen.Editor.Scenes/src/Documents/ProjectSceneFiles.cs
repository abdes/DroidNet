// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>The authored scene files of the session's current project, through its file system.</summary>
/// <param name="session">The project session.</param>
/// <remarks>
/// A scene's identity, such as <c>Watchtower.oscene</c>, names its source, <c>Watchtower.oscene.json</c>, as
/// <see cref="SceneFiles.ReadAsync"/> reads it. Writing the file is what saving the scene does: the asset catalog sees the
/// change, and the automatic cook cooks it when Cook on change is on.
/// </remarks>
internal sealed class ProjectSceneFiles(IProjectSession session) : IAuthoredSceneFiles
{
    /// <inheritdoc/>
    public byte[]? Read(AssetPath scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var project = session.Context;
        if (project.FindSource(scene) is not { IsProjectFile: true } location)
        {
            return null;
        }

        try
        {
            return project.FileSystem.File.ReadAllBytes(location.File.FullPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// <inheritdoc/>
    public void Write(AssetPath scene, byte[] content)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(content);
        var project = session.Context;
        var location = project.FindSource(scene) is { IsProjectFile: true } found
            ? found
            : throw new IOException($"{scene.Name} is not a scene file of the project.");
        project.FileSystem.File.WriteAllBytes(location.File.FullPath, content);
    }
}
