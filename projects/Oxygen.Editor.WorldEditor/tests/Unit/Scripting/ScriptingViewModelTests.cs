// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector.Scripting;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// The Scripting section shows one card per slot with one field per declared parameter, and turns field edits into
/// slot edits.
/// </summary>
[TestClass]
public sealed partial class ScriptingViewModelTests : IDisposable
{
    private const string MovePath = @"C:\Project\Content\Player\move.lua";
    private static readonly Uri MoveScript = new("asset:///Content/Player/move.oscript");

    private static readonly ScriptParameter Speed = new("move_speed", ScriptParameterKind.Float, 1f, 0, Min: 0, Max: 10, Step: 0.5, "m/s", "How fast", [], IsColor: false);
    private static readonly ScriptParameter Tint = new("tint", ScriptParameterKind.Vec3, Vector3.One, 0, Min: null, Max: null, Step: null, string.Empty, string.Empty, [], IsColor: true);
    private static readonly ScriptParameter Mode = new("mode", ScriptParameterKind.String, "walk", 0, Min: null, Max: null, Step: null, string.Empty, string.Empty, ["walk", "run"], IsColor: false);
    private static readonly ScriptParameter Follow = new("follow", ScriptParameterKind.Node, DefaultValue: null, 0, Min: null, Max: null, Step: null, string.Empty, string.Empty, [], IsColor: false);

    private readonly Subject<ScriptAnalysisSnapshot> changes = new();
    private readonly List<ScriptAnalysisSnapshot> current = [];
    private readonly Scene scene = new(Mock.Of<IProject>()) { Name = "Main" };
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
    public void Dispose()
    {
        this.changes.Dispose();
        this.tab?.Dispose();
    }

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

        this.current.Clear();
        this.current.Add(new ScriptAnalysisSnapshot(MovePath, MoveScript, [Problem()], [Speed], ParametersOutOfDate: true));
        this.changes.OnNext(this.current[0]);

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
        var library = new Uri("asset:///.cooked/Scripts/rotate.oscript");
        this.scripting.Slots = [new ScriptSlot { Script = library }.WithOverride("speed", ScriptValue.FromFloat(2f))];
        var declarations = new Mock<IScriptDeclarationProvider>();
        _ = declarations.Setup(provider => provider.ReadAsync(It.IsAny<ProjectContext>(), library, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new CookedScriptDeclarations(library, Guid.NewGuid(), [new CookedScriptParameter("rotation_angle", CookedScriptParameterKind.Float, 0.9f, 0)]));
        using var model = this.CreateModel(declarations: declarations.Object);

        model.UpdateValues([this.player]);

        var card = model.Slots.Should().ContainSingle().Subject;
        _ = card.Fields.Select(static field => (field.Name, field.Kind)).Should().Equal(("rotation_angle", ScriptFieldKind.Number));
        _ = card.Fields[0].NumberValue.Should().Be(0.9f);
        _ = card.StaleValues.Select(static value => value.Name).Should().Equal("speed");
        _ = card.CanEditScript.Should().BeFalse();
        _ = card.EditToolTip.Should().Contain("cooked library");
        _ = card.Notice.Should().Contain("cooked library");
    }

    private static ScriptProblem Problem()
        => new(ScriptProblemSeverity.Error, "S0101", "unexpected symbol", MovePath, 3, 1, "rendered", "explanation");

    private void Analyze(params ScriptParameter[] parameters)
        => this.current.Add(new ScriptAnalysisSnapshot(MovePath, MoveScript, [], parameters, ParametersOutOfDate: false));

    private ScriptingViewModel CreateModel(Action? onEdit = null, IScriptDeclarationProvider? declarations = null)
    {
        // The document edits its own copy of the scene: the test reads the player and its slots from it.
        this.tab = SceneTabs.Open(this.scene);
        this.player = this.tab.NodeOf(this.player);
        this.scripting = this.player.Components.OfType<ScriptingComponent>().Single();
        if (onEdit is not null)
        {
            this.tab.Authoring.Changed += (_, _) => onEdit();
        }

        var analysis = new Mock<IScriptAnalysisService>();
        _ = analysis.SetupGet(service => service.Changes).Returns(this.changes);
        _ = analysis.SetupGet(service => service.Current).Returns(this.current);
        var projects = new ProjectContextService();
        projects.Activate(new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Project",
            Category = Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        });
        return new ScriptingViewModel(() => this.tab.Authoring, analysis: analysis.Object, projects: projects, declarationProvider: declarations);
    }
}
