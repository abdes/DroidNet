// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>The text of a new scene, and the template New Scene creates it from.</summary>
public static class SceneTemplates
{
    /// <summary>Gets the template of New Scene: an empty scene in <c>Scenes</c>, named after its file.</summary>
    public static AuthoredAssetTemplate Asset { get; } = new()
    {
        Folder = Constants.ScenesFolderName,
        DefaultName = "NewScene",
        Suffixes = [AssetFileTypes.SceneDescriptor],
        Content = New,
    };

    /// <summary>Makes the text of a new, empty scene, with an identity of its own.</summary>
    /// <param name="sceneName">The scene's name, which is its file name.</param>
    /// <returns>The scene file's text.</returns>
    public static string New(string sceneName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sceneName);
        return $$"""
            {
              "Id": "{{Guid.NewGuid():D}}",
              "Name": "{{sceneName}}",
              "RootNodes": []
            }

            """;
    }
}
