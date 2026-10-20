// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>Checks the cook kind of each file the pipeline cooks.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ContentCookAssetKindsTests
{
    [TestMethod]
    [DataRow("/Content/Textures/Sky.otex.json", ContentCookAssetKind.Texture)]
    [DataRow("/Content/Textures/Sky.otex", ContentCookAssetKind.Texture)]
    [DataRow("/Content/Materials/Red.omat.json", ContentCookAssetKind.Material)]
    [DataRow("/Content/Geometry/Box.ogeo", ContentCookAssetKind.Geometry)]
    [DataRow("/Content/Scenes/Main.oscene.json", ContentCookAssetKind.Scene)]
    [DataRow("/Content/Models/Ship.GLB", ContentCookAssetKind.ForeignSource)]
    [DataRow("/Content/Input/Player.input.json", ContentCookAssetKind.Input)]
    [DataRow("/Content/Scripts/Door.lua", ContentCookAssetKind.Unknown)]
    [DataRow("/Content/Materials/Red.OMAT.json", ContentCookAssetKind.Unknown)]
    [DataRow("/Content/Notes/readme.md", ContentCookAssetKind.Unknown)]
    public void FileTypeGivesTheCookKind(string path, ContentCookAssetKind expected)
        => _ = ContentCookAssetKinds.Of(path).Should().Be(expected);

    [TestMethod]
    [DataRow("Red.omat.json", true)]
    [DataRow("Player.input.json", true)]
    [DataRow("Red.omat", false)]
    [DataRow("Door.lua", false)]
    public void OnlyAuthoredSourcesAreCookable(string name, bool expected)
    {
        var project = TestProject.Create(new MockFileSystem());

        _ = ContentPipelineService.IsCookableDescriptorFile(project.Context, project.PathOf("/Content/" + name).FullPath).Should().Be(expected);
    }
}
