// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Input.Tests;

/// <summary>
/// An input source's file follows from its identity, in an authoring mount of the project, and reads as the cooker
/// reads it.
/// </summary>
[TestClass]
public sealed class InputSourceFilesTests
{
    private static readonly AssetPath Player = AssetPath.Parse("/Content/Input/Player.input");

    private readonly TestProject project;
    private readonly InputSourceFiles files;

    public InputSourceFilesTests()
    {
        var fileSystem = new MockFileSystem();
        this.project = TestProject.Create(fileSystem);
        this.files = new InputSourceFiles(fileSystem);
    }

    [TestMethod]
    public void TheFileIsTheIdentityWithItsAuthoringSuffix()
    {
        _ = InputSourceFiles.FileOf(this.project.Context, Player).Should().Be(this.project.PathOf("/Content/Input/Player.input.json"));
        _ = InputSourceFiles.FileOf(this.project.Context, AssetPath.Parse("/Engine/Input/Engine.input")).Should().BeNull("the project does not author engine content");
    }

    [TestMethod]
    public void ASourceReadsAsTheCookerReadsIt()
    {
        _ = this.project.WriteFile("/Content/Input/Player.input.json", InputTemplates.New("Player"));

        var player = this.files.Read(this.project.Context, Player);

        _ = player!.Identity.Should().Be(Player);
        _ = player.Contexts.Should().ContainSingle().Which.Identity.Should().Be(AssetPath.Parse("/Content/Input/Player.input/Player.oimap"));
    }

    [TestMethod]
    public void AMissingOrInvalidSourceReadsAsNone()
    {
        _ = this.files.Read(this.project.Context, Player).Should().BeNull();

        _ = this.project.WriteFile("/Content/Input/Player.input.json", """{ "actions": 3 }""");

        _ = this.files.Read(this.project.Context, Player).Should().BeNull();
    }
}
