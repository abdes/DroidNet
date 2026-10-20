// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>Opens a scene a test built in memory as a scene document, and gives the document, its tab and its scene.</summary>
/// <remarks>
/// The scene is written to a file of an in-memory project and opened through the scene kind, as the editor opens it. The document
/// therefore edits a scene of its own: a test takes the document's scene and nodes from the <see cref="SceneTab"/>.
/// </remarks>
internal static class SceneTabs
{
    /// <summary>Opens a scene as a document of its own.</summary>
    /// <param name="scene">The scene the document's file records.</param>
    /// <param name="interaction">The lock and hide state the document's edits respect, if any.</param>
    /// <param name="redirects">The asset moves and deletions the document's edits see, if any.</param>
    /// <param name="layout">The explorer layout the file records beside the scene, if any.</param>
    /// <returns>The scene's document, with its tab; the caller disposes it.</returns>
    public static SceneTab Open(Scene scene, ISceneNodeInteraction? interaction = null, IAssetRedirects? redirects = null, IList<ExplorerEntryData>? layout = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), @"C:\SceneTabs"), interaction, redirects);
        try
        {
            var name = string.IsNullOrEmpty(scene.Name) ? "Scene" : scene.Name;
            var file = documents.FileOf(name);
            documents.Write(file, scene.Dehydrate(), layout);
            return new SceneTab(documents, file);
        }
        catch
        {
            documents.Dispose();
            throw;
        }
    }
}
