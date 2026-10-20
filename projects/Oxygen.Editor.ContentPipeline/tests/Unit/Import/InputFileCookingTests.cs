// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>
/// Input sources cook as <c>input</c> jobs named by their identity, and their actions and contexts resolve to the
/// source they are named under.
/// </summary>
[TestClass]
public sealed class InputFileCookingTests : IDisposable
{
    private const string Player = """
        {
          "actions": [ { "name": "Jump", "type": "bool" }, { "name": "Dash", "type": "bool" } ],
          "contexts": [
            { "name": "Gameplay", "priority": 3, "mappings": [
              { "action": "Jump", "slot": "Space", "trigger": "pressed" },
              { "action": "/Content/Common/Common.input/Fire.oiact", "slot": "F" },
              { "action": "/Content/Common/Common.input/Missing.oiact", "slot": "G" },
              { "action": "/Engine/Input/Engine.input/Pause.oiact", "slot": "Escape" }
            ] }
          ]
        }
        """;

    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-input-cooking-tests", Guid.NewGuid().ToString("N"));

    public InputFileCookingTests()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Gameplay"));
    }

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    public void ResolveInputFileKeepsItsIdentity()
    {
        var input = CookInputResolver.Resolve(this.CreateProject(), AssetPath.Parse("/Content/Gameplay/Player.input.json"), ContentCookInputRole.Primary);

        _ = input.Kind.Should().Be(ContentCookAssetKind.Input);
        _ = input.SourceRelativePath.Should().Be("Content/Gameplay/Player.input.json");
        _ = input.AssetUri.Should().Be(AssetPath.Parse("/Content/Gameplay/Player.input"), "an input source stands for its path without the authoring .json");
    }

    [TestMethod]
    public void BuildJobNamesTheSourceIdentity()
    {
        var input = CookInputResolver.Resolve(this.CreateProject(), AssetPath.Parse("/Content/Gameplay/Player.input.json"), ContentCookInputRole.Primary);

        var job = new ContentImportManifestBuilder().BuildJob(input, []);

        _ = job.Type.Should().Be("input");
        _ = job.Id.Should().Be("input:Content/Gameplay/Player.input.json");
        _ = job.Name.Should().BeNull("input jobs accept no name");
        _ = job.VirtualPath.Should().Be("/Content/Gameplay/Player.input", "the cooker names the source's products under its identity");
        _ = job.Layout.Should().Be(new ContentImportLayout("/Content"));
    }

    [TestMethod]
    public void AProductResolvesToTheContainerItIsNamedUnder()
    {
        var project = this.CreateProject();
        _ = this.Write("Gameplay/Player.input.json", Player);
        _ = this.Write("Gameplay/Ship.glb", "model");

        var source = CookInputResolver.ResolveContainer(project, AssetPath.Parse("/Content/Gameplay/Player.input/Jump.oiact"), ContentCookInputRole.Dependency);

        _ = source!.AssetUri.Should().Be(AssetPath.Parse("/Content/Gameplay/Player.input"));
        _ = source.Kind.Should().Be(ContentCookAssetKind.Input);
        _ = source.Role.Should().Be(ContentCookInputRole.Dependency);
        _ = CookInputResolver.ResolveContainer(project, AssetPath.Parse("/Content/Gameplay/Vehicle.input/Jump.oiact"), ContentCookInputRole.Dependency)
            .Should().BeNull("no source of the project is named Vehicle.input");
        _ = CookInputResolver.ResolveContainer(project, AssetPath.Parse("/Content/Gameplay/Ship.glb/Paint.omat"), ContentCookInputRole.Dependency)
            .Should().BeNull("a model produces nothing until it is imported");
        _ = CookInputResolver.ResolveContainer(project, AssetPath.Parse("/Content/Gameplay/Player.input"), ContentCookInputRole.Dependency)
            .Should().BeNull("a container is not its own product");
    }

    private string Write(string relativePath, string text)
    {
        var path = Path.GetFullPath(Path.Combine(this.root, "Content", relativePath));
        File.WriteAllText(path, text);
        return path;
    }

    private ProjectContext CreateProject()
        => ProjectContext.FromProject(new Project(new ProjectInfo("Test", Category.Games, this.root)
        {
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        }), new RealFileSystem());
}
