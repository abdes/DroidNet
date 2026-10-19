// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Authoring;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>
/// Input sources cook as <c>input</c> jobs named by their identity, and their actions and contexts are found in the
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

    private const string Common = """
        { "actions": [ { "name": "Fire", "type": "bool" } ] }
        """;

    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-input-cooking-tests", Guid.NewGuid().ToString("N"));

    public InputFileCookingTests()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Gameplay"));
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Common"));
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
    public void AProductIsDeclaredInTheSourceItIsNamedUnder()
    {
        var project = this.CreateProject();
        var file = this.Write("Gameplay/Player.input.json", Player);

        var action = InputSourceFiles.FindDeclaration(project, AssetPath.Parse("/Content/Gameplay/Player.input/Dash.oiact"));
        var context = InputSourceFiles.FindDeclaration(project, AssetPath.Parse("/Content/Gameplay/Player.input/Gameplay.oimap"));

        _ = action.Should().NotBeNull();
        _ = action!.File.FullPath.Should().Be(file);
        _ = action.Position.Should().Be(new SourcePosition(2, 62));
        _ = context!.Position.Line.Should().Be(4);
        _ = InputSourceFiles.FindDeclaration(project, AssetPath.Parse("/Content/Gameplay/Player.input/Gameplay.oiact")).Should().BeNull();
        _ = InputSourceFiles.FindDeclaration(project, AssetPath.Parse("/Content/Gameplay/Vehicle.input/Dash.oiact")).Should().BeNull();
    }

    [TestMethod]
    public void AContextTakesThePriorityItsSourceDeclares()
    {
        var project = this.CreateProject();
        _ = this.Write("Gameplay/Player.input.json", Player);

        _ = InputContextSources.Find(project, AssetPath.Parse("/Content/Gameplay/Player.input/Gameplay.oimap"))
            .Should().Be(new InputContextSource(IsProjectContext: true, DeclaredPriority: 3));
        _ = InputContextSources.Find(project, AssetPath.Parse("/Content/Gameplay/Player.input/Menu.oimap")).IsUndeclared.Should().BeTrue();
        _ = InputContextSources.Find(project, AssetPath.Parse("/Content/Gameplay/Gameplay.oimap")).IsUndeclared.Should().BeTrue("a context is declared only by the source it is named under");
    }

    [TestMethod]
    public void AnActionNamedByPathMustBeDeclaredByAProjectSource()
    {
        var project = this.CreateProject();
        _ = this.Write("Common/Common.input.json", Common);
        var source = InputSourceReader.Read(File.ReadAllBytes(this.Write("Gameplay/Player.input.json", Player)));

        var problems = InputSourceFiles.CheckActionPaths(project, AssetPath.Parse("/Content/Gameplay/Player.input"), source).ToList();

        var problem = problems.Should().ContainSingle("Fire is declared, and the engine content is resolved by the cook").Subject;
        _ = problem.Code.Should().Be(InputSourceCodes.ActionMissing);
        _ = problem.Line.Should().Be(7);
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
        }));
}
