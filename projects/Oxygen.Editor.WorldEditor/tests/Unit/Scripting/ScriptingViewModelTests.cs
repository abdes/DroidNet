// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Scripting.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector.Scripting;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// The Scripting section shows one card per slot with one field per declared parameter, and turns field edits into
/// slot edits.
/// </summary>
[TestClass]
public sealed partial class ScriptingViewModelTests : IDisposable
{
    private const string MovePath = @"C:\Project\Content\Player\move.lua";
    private static readonly AssetPath MoveScript = AssetPath.Parse("/Content/Player/move.oscript");

    private static readonly ScriptParameter Speed = new()
    {
        Name = "move_speed",
        Kind = ScriptParameterKind.FloatParameter,
        Default = ScriptValue.FromFloat(1f),
        Min = 0,
        Max = 10,
        Step = 0.5,
        Unit = "m/s",
        Description = "How fast",
    };

    private static readonly ScriptParameter Tint = new() { Name = "tint", Kind = ScriptParameterKind.Vec3Parameter, Default = ScriptValue.FromVector(Vector3.One), IsColor = true };
    private static readonly ScriptParameter Mode = new() { Name = "mode", Kind = ScriptParameterKind.StringParameter, Default = ScriptValue.FromString("walk"), Options = ["walk", "run"] };
    private static readonly ScriptParameter Follow = new() { Name = "follow", Kind = ScriptParameterKind.NodeParameter };

    private readonly FakeScripts scripts = new();
    private readonly Scene scene = new() { Name = "Main" };
    private SceneNode player;
    private SceneTab? tab;
    private ScriptingComponent scripting = new() { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot { Script = MoveScript }] };

    public ScriptingViewModelTests()
    {
        this.player = new SceneNode(this.scene) { Name = "Player" };
        this.scene.RootNodes.Add(this.player);
        _ = this.player.AddComponent(this.scripting);
    }

    /// <inheritdoc />
    public void Dispose() => this.tab?.Dispose();

    [TestMethod]
    public void OneCardPerSlotWithOneFieldPerDeclaredParameter()
    {
        this.Analyze(Speed, Tint, Mode, Follow);
        using var model = this.CreateModel();

        model.UpdateValues([this.player]);

        var card = model.Slots.Should().ContainSingle().Subject;
        _ = card.Fields.Select(static field => (field.Name, field.Kind)).Should().Equal(
            ("move_speed", ScriptFieldKind.Number), ("tint", ScriptFieldKind.Color), ("mode", ScriptFieldKind.Choice), ("follow", ScriptFieldKind.Node));
        var speed = card.Fields[0];
        _ = speed.Label.Should().Be("Move speed");
        _ = speed.NumberValue.Should().Be(1f);
        _ = speed.NumberMask.Should().Be("~.#");
        _ = speed.IsOverridden.Should().BeFalse();
        _ = card.Fields[3].ReferenceText.Should().Be("None");
    }

    [TestMethod]
    public void SeveralSelectedNodesShowAMessageInsteadOfSlots()
    {
        using var model = this.CreateModel();
        var other = new SceneNode(this.scene) { Name = "Other" };

        model.UpdateValues([this.player, other]);

        _ = model.ShowsSlots.Should().BeFalse();
        _ = model.SelectionMessage.Should().NotBeEmpty();
        _ = model.Slots.Should().BeEmpty();
    }

    [TestMethod]
    public async Task EditingAFieldStoresAClampedOverrideAndTheDefaultRemovesIt()
    {
        this.Analyze(Speed);
        using var model = this.CreateModel();
        model.UpdateValues([this.player]);
        var field = model.Slots[0].Fields[0];

        field.NumberValue = 42f;
        await model.Pending.ConfigureAwait(false);

        _ = this.scripting.Slots[0].Overrides["move_speed"].FloatValue.Should().Be(10f, "the declared maximum is 10");
        _ = model.Slots[0].Fields[0].IsOverridden.Should().BeTrue();

        model.Slots[0].Fields[0].NumberValue = 1f;
        await model.Pending.ConfigureAwait(false);

        _ = this.scripting.Slots[0].Overrides.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ADraggedNumberCommitsOnceWhenTheSessionCompletes()
    {
        this.Analyze(Speed);
        var edits = 0;
        using var model = this.CreateModel(() => edits++);
        model.UpdateValues([this.player]);
        var field = model.Slots[0].Fields[0];

        field.BeginSession();
        field.NumberValue = 2f;
        field.NumberValue = 3f;
        field.CompleteSession(commit: true);
        await model.Pending.ConfigureAwait(false);

        _ = edits.Should().Be(1);
        _ = this.scripting.Slots[0].Overrides["move_speed"].FloatValue.Should().Be(3f);
    }

    [TestMethod]
    public void StaleValuesAndOutOfDateDeclarationsAreShown()
    {
        this.scripting.Slots = [this.scripting.Slots[0].WithOverride("gone", ScriptValue.FromBool(value: true))];
        this.Analyze(Speed);
        using var model = this.CreateModel();

        model.UpdateValues([this.player]);

        var stale = model.Slots[0].StaleValues.Should().ContainSingle().Subject;
        _ = (stale.Name, stale.TypeText, stale.ValueText).Should().Be(("gone", "bool", "true"));
        _ = model.Slots[0].HasStaleValues.Should().BeTrue();
        _ = model.Slots[0].CanRemoveAllStale.Should().BeFalse("one value is removed with its own button");

        _ = this.scripts.Add(Analyzed(Speed) with { Problems = [Problem()], ParametersOutOfDate = true });

        _ = model.Slots[0].ParametersOutOfDate.Should().BeTrue();
        _ = model.Slots[0].HasBadge.Should().BeTrue();
        _ = model.Slots[0].BadgeIsError.Should().BeTrue();
        _ = model.Slots[0].BadgeText.Should().Be("1 error");
        _ = model.Slots[0].BadgeToolTip.Should().StartWith("Error S0101, line 3: unexpected symbol");
        _ = model.Slots[0].StaleValues.Should().BeEmpty("values are not judged against out-of-date declarations");
    }

    [TestMethod]
    public async Task AddingAndMovingSlotsEditTheComponent()
    {
        using var model = this.CreateModel();
        model.UpdateValues([this.player]);

        await model.AddSlotCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        await model.Slots[1].MoveUpCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = this.scripting.Slots.Should().HaveCount(2);
        _ = this.scripting.Slots[1].Script.Should().Be(MoveScript);
        _ = model.Slots[0].CanMoveUp.Should().BeFalse();
        _ = model.Slots[1].CanMoveDown.Should().BeFalse();
    }

    [TestMethod]
    public async Task AStaleValueIsRemovedOnItsOwn()
    {
        this.scripting.Slots = [this.scripting.Slots[0]
            .WithOverride("gone", ScriptValue.FromBool(value: true))
            .WithOverride("old", ScriptValue.FromFloat(2f))
            .WithOverride("move_speed", ScriptValue.FromFloat(3f))];
        this.Analyze(Speed);
        using var model = this.CreateModel();
        model.UpdateValues([this.player]);
        _ = model.Slots[0].CanRemoveAllStale.Should().BeTrue();

        await model.Slots[0].StaleValues.Single(static value => string.Equals(value.Name, "old", StringComparison.Ordinal)).RemoveCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = this.scripting.Slots[0].Overrides.Keys.Should().BeEquivalentTo("gone", "move_speed");
        _ = model.Slots[0].StaleValues.Select(static value => value.Name).Should().Equal("gone");
    }

    [TestMethod]
    public void ALibraryScriptShowsItsCookedParametersAndHasNoSourceToEdit()
    {
        var library = AssetPath.Parse("/Engine/Scripts/rotate.oscript");
        this.scripting.Slots = [new ScriptSlot { Script = library }.WithOverride("speed", ScriptValue.FromFloat(2f))];
        var rotate = new Script
        {
            Identity = library,
            Origin = ScriptOrigin.Library,
            Parameters = [new ScriptParameter { Name = "rotation_angle", Kind = ScriptParameterKind.FloatParameter, Default = ScriptValue.FromFloat(0.9f) }],
        };
        this.scripts.SetLibrary(library, _ => Task.FromResult(ScriptRead.Found(rotate)));
        using var model = this.CreateModel();

        model.UpdateValues([this.player]);

        var card = model.Slots.Should().ContainSingle().Subject;
        _ = card.Fields.Select(static field => (field.Name, field.Kind)).Should().Equal(("rotation_angle", ScriptFieldKind.Number));
        _ = card.Fields[0].NumberValue.Should().Be(0.9f);
        _ = card.StaleValues.Select(static value => value.Name).Should().Equal("speed");
        _ = card.CanEditScript.Should().BeFalse();
        _ = card.EditToolTip.Should().Contain("cooked library");
        _ = card.Notice.Should().Contain("cooked library");
    }

    [TestMethod]
    public void ALibraryScriptThatCannotBeReadSaysWhyAndIsReadOnce()
    {
        var library = AssetPath.Parse("/Engine/Scripts/rotate.oscript");
        this.scripting.Slots = [new ScriptSlot { Script = library }];
        this.scripts.SetLibrary(library, _ => Task.FromResult(ScriptRead.Unavailable("the library is gone")));
        using var model = this.CreateModel();

        model.UpdateValues([this.player]);
        model.UpdateValues([this.player]);

        var card = model.Slots.Should().ContainSingle().Subject;
        _ = card.Notice.Should().Contain("the library is gone");
        _ = card.IsScriptMissing.Should().BeFalse("the script exists in a library that could not be read");
        _ = this.scripts.LibraryReads.Should().Be(1);
    }

    private static ScriptProblem Problem()
        => new(DiagnosticSeverity.Error, "S0101", "unexpected symbol", "rendered") { File = MovePath, Line = 3, Column = 1, Explanation = "explanation" };

    private static Script Analyzed(params ScriptParameter[] parameters)
        => new() { Identity = MoveScript, Origin = ScriptOrigin.Source, SourcePath = FileSystemPath.From(MovePath), Parameters = parameters };

    private void Analyze(params ScriptParameter[] parameters) => _ = this.scripts.Add(Analyzed(parameters));

    private ScriptingViewModel CreateModel(Action? onEdit = null)
    {
        // The document edits its own copy of the scene: the test reads the player and its slots from it.
        this.tab = SceneTabs.Open(this.scene);
        this.player = this.tab.NodeOf(this.player);
        this.scripting = this.player.Components.OfType<ScriptingComponent>().Single();
        if (onEdit is not null)
        {
            this.tab.Authoring.Changed += (_, _) => onEdit();
        }

        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new MockFileSystem()).Context with { Name = "Project" });
        return new ScriptingViewModel(() => this.tab.Authoring, scripts: this.scripts, projects: projects);
    }
}
