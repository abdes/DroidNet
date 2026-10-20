// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Input.Tests;

/// <summary>
/// A scene's starting input context is declared by the input source it is named under, starts with the priority the
/// scene gives it or else its own, and does not cook when the project's source does not declare it.
/// </summary>
[TestClass]
public sealed class StartingContextRulesTests
{
    private static readonly AssetPath Gameplay = AssetPath.Parse("/Content/Input/Player.input/Gameplay.oimap");
    private static readonly AssetPath Menu = AssetPath.Parse("/Content/Input/Player.input/Menu.oimap");

    private readonly TestProject project = TestProject.Create(new MockFileSystem());
    private readonly FakeInputSources inputs = new();

    public StartingContextRulesTests()
    {
        const string PlayerInputJson = """
            {
              "actions": [ { "name": "Jump", "type": "bool" } ],
              "contexts": [ { "name": "Gameplay", "priority": 3, "mappings": [ { "action": "Jump", "slot": "Space" } ] } ]
            }
            """;

        _ = this.inputs.Add("/Content/Input/Player.input", PlayerInputJson);
    }

    [TestMethod]
    public void AProjectContextTakesThePriorityItsSourceDeclares()
        => _ = this.Describe(Gameplay).Should().Be(new InputContextSource(IsProjectContext: true, DeclaredPriority: 3));

    [TestMethod]
    public void AContextItsSourceDoesNotDeclareIsUndeclared()
    {
        _ = this.Describe(Menu).IsUndeclared.Should().BeTrue();
        _ = this.Describe(AssetPath.Parse("/Content/Input/Gameplay.oimap")).IsUndeclared.Should().BeTrue("a context is declared only by the source it is named under");
        _ = this.Describe(AssetPath.Parse("/Content/Input/Vehicle.input/Gameplay.oimap")).IsUndeclared.Should().BeTrue("the project has no such source");
    }

    [TestMethod]
    public void AContextOutsideTheProjectIsExternal()
    {
        _ = this.Describe(AssetPath.Parse("/Engine/Input/Engine.input/Menu.oimap")).Should().Be(InputContextSource.External);
        _ = StartingContextRules.Describe(project: null, Gameplay, this.inputs.Find).Should().Be(InputContextSource.External);
    }

    [TestMethod]
    public void TheScenesPriorityWinsOverTheContextsOwn()
    {
        _ = StartingContextRules.EffectivePriority(new StartingInputContextData(Gameplay, 7), this.Describe(Gameplay)).Should().Be(7);
        _ = StartingContextRules.EffectivePriority(new StartingInputContextData(Gameplay), this.Describe(Gameplay)).Should().Be(3);
        _ = StartingContextRules.EffectivePriority(new StartingInputContextData(Menu), this.Describe(Menu)).Should().Be(StartingContextRules.DefaultPriority);
    }

    [TestMethod]
    public void OnlyADeclaredMappingContextStarts()
    {
        _ = Check(Gameplay, this.Describe(Gameplay)).Should().BeNull();
        _ = Check(AssetPath.Parse("/Engine/Input/Engine.input/Menu.oimap"), InputContextSource.External).Should().BeNull("the cook resolves engine content");
        _ = Check(Menu, this.Describe(Menu))!.Code.Should().Be(StartingContextCodes.Undeclared);
        _ = Check(AssetPath.Parse("/Content/Input/Player.input/Jump.oiact"), InputContextSource.External)!.Code.Should().Be(StartingContextCodes.NotAContext);
    }

    private static Managed.Assets.Diagnostics.AssetProblem? Check(AssetPath context, InputContextSource source)
        => StartingContextRules.Check(new StartingInputContextData(context), source);

    private InputContextSource Describe(AssetPath context) => StartingContextRules.Describe(this.project.Context, context, this.inputs.Find);
}
