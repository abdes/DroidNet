// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Scenes.Tests.Documents;

/// <summary>A new scene is an empty scene file, named after its file, with an identity of its own.</summary>
[TestClass]
public sealed class SceneTemplatesTests
{
    [TestMethod]
    public void NewSceneReadsAsAnEmptySceneNamedAfterItsFile()
    {
        var scene = JsonSerializer.Deserialize(SceneTemplates.New("Intro"), SceneJsonContext.Default.SceneData)!;

        _ = scene.Name.Should().Be("Intro");
        _ = scene.RootNodes.Should().BeEmpty();
    }

    [TestMethod]
    public void EachNewSceneHasItsOwnIdentity()
    {
        var intro = JsonSerializer.Deserialize(SceneTemplates.New("Intro"), SceneJsonContext.Default.SceneData)!;
        var outro = JsonSerializer.Deserialize(SceneTemplates.New("Outro"), SceneJsonContext.Default.SceneData)!;

        _ = intro.Id.Should().NotBe(outro.Id);
    }

    [TestMethod]
    public void TemplateCreatesSceneDescriptorsInTheScenesFolder()
    {
        _ = SceneTemplates.Asset.Folder.Should().Be("Scenes");
        _ = SceneTemplates.Asset.Suffixes.Should().Equal(AssetFileTypes.SceneDescriptor);
    }
}
