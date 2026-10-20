// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Authoring;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Input.Tests;

/// <summary>
/// An input source's products are identified under it, and an action it names by path must be declared by a source of
/// the project; a new source's template checks clean.
/// </summary>
[TestClass]
public sealed class InputRulesTests
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

    private readonly TestProject project = TestProject.Create(new MockFileSystem());
    private readonly FakeInputSources inputs = new();

    [TestMethod]
    public void AProductIsIdentifiedUnderItsSource()
    {
        var player = this.inputs.Add("/Content/Gameplay/Player.input", Player);

        _ = player.Actions.Select(static action => action.Identity.VirtualPath).Should().Equal(
            "/Content/Gameplay/Player.input/Jump.oiact",
            "/Content/Gameplay/Player.input/Dash.oiact");
        _ = player.FindContext(AssetPath.Parse("/Content/Gameplay/Player.input/Gameplay.oimap"))!.Priority.Should().Be(3);
        _ = player.FindDeclaration(AssetPath.Parse("/Content/Gameplay/Player.input/Dash.oiact")).Should().Be(new SourcePosition(2, 62));
        _ = player.FindDeclaration(AssetPath.Parse("/Content/Gameplay/Player.input/Gameplay.oiact")).Should().BeNull("Gameplay is a context");
        _ = player.FindDeclaration(AssetPath.Parse("/Content/Gameplay/Vehicle.input/Dash.oiact")).Should().BeNull("Dash of another source");
    }

    [TestMethod]
    public void AnActionNamedByPathMustBeDeclaredByAProjectSource()
    {
        _ = this.inputs.Add("/Content/Common/Common.input", """{ "actions": [ { "name": "Fire", "type": "bool" } ] }""");
        var player = this.inputs.Add("/Content/Gameplay/Player.input", Player);

        var problems = InputRules.CheckActionPaths(this.project.Context, player, this.inputs.Find).ToList();

        var problem = problems.Should().ContainSingle("Fire is declared, and the cook resolves the engine content").Subject;
        _ = problem.Code.Should().Be(InputSourceCodes.ActionMissing);
        _ = problem.Line.Should().Be(7);
    }

    [TestMethod]
    public void TheNewSourceTemplateChecksClean()
    {
        var report = InputSourceReader.Check(Encoding.UTF8.GetBytes(InputTemplates.New("Player")));

        _ = report.Problems.Should().BeEmpty();
        _ = report.Source!.Contexts.Should().ContainSingle().Which.Name.Should().Be("Player");
    }
}
