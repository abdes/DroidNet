// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.Edits;

/// <summary>
/// The obligations every scene edit meets (scene-edits.md 3.2, 3.3, 3.11 and the mandatory edit rule 1 of
/// scenes-module.md 3.6), run once per edit type over <see cref="EditSamples"/>.
/// </summary>
[TestClass]
public sealed class EditContractTests
{
    private const BindingFlags InstanceMembers = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    /// <summary>Gets every public <see cref="ISceneEdit"/> type of the Scenes assembly, one row each.</summary>
    public static IEnumerable<TestDataRow<Type>> SceneEditTypes
        => PublicSceneEditTypes().Select(static type => new TestDataRow<Type>(type) { DisplayName = type.Name });

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task RoundTrip_RestoresTheSceneExactly(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);

        var result = await SceneAssert.RoundTripAsync(fixture, sample.Build(SampleScene.Build())).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task Apply_RaisesTheDeclaredChanges(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var edit = sample.Build(SampleScene.Build());

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Changes(Normalized(sample, fixture.Changes.Only()), Normalized(sample, sample.Expected));
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task Undo_RaisesTheChangesOfTheRevert(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(sample.Build(SampleScene.Build())).ConfigureAwait(false);
        fixture.Changes.Clear();

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        SceneAssert.Changes(Normalized(sample, fixture.Changes.Only()), Normalized(sample, sample.ExpectedUndo));
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.WithTargets), typeof(EditSamples))]
    public async Task MissingTarget_IsRefused(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        var edit = sample.Build(SampleScene.Build());
        var missing = edit.Targets.First();
        using var fixture = await OpenAsync(sample, SampleScene.Builder().Without(missing).Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.WithTargets), typeof(EditSamples))]
    public async Task LockedTarget_IsRefused(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var edit = sample.Build(SampleScene.Build());
        var locked = edit.Targets.First();
        fixture.Interaction!.Lock(locked);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, locked);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.WithNestedTarget), typeof(EditSamples))]
    public async Task TargetUnderLockedAncestor_IsRefused(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var edit = sample.Build(SampleScene.Build());
        var target = edit.Targets.First();
        fixture.Interaction!.Lock(EditSamples.ParentOf(target)!.Value);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, target);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task AfterDispose_IsRetired(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var edit = sample.Build(SampleScene.Build());
        fixture.Authoring.Dispose();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Retired(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task Validate_ChangesNothing(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var edit = sample.Build(SampleScene.Build());
        var before = fixture.Snapshot();

        var validation = await fixture.Authoring.ValidateAsync(edit).ConfigureAwait(false);

        _ = validation.IsValid.Should().BeTrue("the sample is a valid edit of the sample scene");
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task Json_RoundTripsTheEdit(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var expected = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        using var actual = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var edit = sample.Build(SampleScene.Build());
        var json = JsonSerializer.Serialize(edit, SceneEditJson.Options);
        var copy = JsonSerializer.Deserialize<ISceneEdit>(json, SceneEditJson.Options)!;
        _ = await expected.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        var result = await actual.Authoring.ExecuteAsync(copy).ConfigureAwait(false);

        _ = copy.Should().BeOfType(edit.GetType());
        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = JsonSerializer.Serialize(copy, SceneEditJson.Options).Should().Be(json);
        _ = StateOf(actual.Snapshot()).Should().Be(StateOf(expected.Snapshot()));
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.All), typeof(EditSamples))]
    public async Task Json_RoundTripsTheRevert(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var applied = await ApplyInProbeAsync(fixture, sample.Build(SampleScene.Build())).ConfigureAwait(false);
        var json = JsonSerializer.Serialize(applied.Revert, SceneEditJson.Options);
        var revert = JsonSerializer.Deserialize<ISceneEdit>(json, SceneEditJson.Options)!;

        _ = await ApplyInProbeAsync(fixture, revert).ConfigureAwait(false);

        _ = StateOf(fixture.Snapshot()).Should().Be(StateOf(before));
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.OwningItsInput), typeof(EditSamples))]
    public async Task ConstructorInput_ChangedAfterwards_DoesNotChangeTheEdit(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var expected = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        using var actual = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        _ = await expected.Authoring.ExecuteAsync(sample.Tamper!(SampleScene.Build()).Edit).ConfigureAwait(false);
        var tampered = sample.Tamper!(SampleScene.Build());
        tampered.Mutate();

        var result = await actual.Authoring.ExecuteAsync(tampered.Edit).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = StateOf(actual.Snapshot()).Should().Be(StateOf(expected.Snapshot()));
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.WithAssetReference), typeof(EditSamples))]
    public async Task Undo_AssetRenamedAfterTheEdit_RestoresTheNewUri(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var asset = sample.Asset!;
        var moved = EditSamples.MovedTo(asset);
        _ = await fixture.Authoring.ExecuteAsync(sample.Build(SampleScene.Build())).ConfigureAwait(false);
        fixture.Redirects.Move(asset, moved);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        SceneAssert.Applied(redone);
        _ = fixture.Snapshot().Document.Should()
            .Contain(moved.Name)
            .And.NotContain(asset.Name);
    }

    [TestMethod]
    [DynamicData(nameof(EditSamples.WithAssetReference), typeof(EditSamples))]
    public async Task Redo_AssetDeletedAfterTheEdit_RestoresWithAWarning(EditSample sample)
    {
        ArgumentNullException.ThrowIfNull(sample);
        using var fixture = await OpenAsync(sample, SampleScene.Build()).ConfigureAwait(false);
        var asset = sample.Asset!;
        _ = await fixture.Authoring.ExecuteAsync(sample.Build(SampleScene.Build())).ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        fixture.Redirects.Delete(asset);

        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        // The reference is kept and shows as missing until the user picks another asset (3.11).
        SceneAssert.Warned(redone, SceneEditCodes.AssetReferenceDeleted, sample.AssetNode);
        _ = fixture.Snapshot().Document.Should().Contain(asset.Name);
    }

    [TestMethod]
    [DynamicData(nameof(SceneEditTypes))]
    public void EditType_HasOnlyAllowedMembers(Type editType)
    {
        ArgumentNullException.ThrowIfNull(editType);

        var forbidden = editType.GetProperties(InstanceMembers).Select(static property => (property.Name, Type: property.PropertyType))
            .Concat(editType.GetFields(InstanceMembers).Select(static field => (field.Name, Type: field.FieldType)))
            .Where(static member => IsForbidden(member.Type))
            .Select(static member => $"{member.Name}: {member.Type}");

        _ = forbidden.Should().BeEmpty("an edit holds ids and values, never live model objects, Guids, lists or arrays");
    }

    [TestMethod]
    public void EverySceneEditType_HasASample()
    {
        var sampled = EditSamples.All.Select(static row => row.Value.Build(SampleScene.Build()).GetType()).ToHashSet();

        var unsampled = PublicSceneEditTypes().Where(type => !sampled.Contains(type)).Select(static type => type.Name);

        _ = unsampled.Should().BeEmpty("every public edit type needs a row in EditSamples.All");
    }

    private static IEnumerable<Type> PublicSceneEditTypes()
        => typeof(ISceneEdit).Assembly.GetExportedTypes()
            .Where(static type => type.IsClass && !type.IsAbstract && typeof(ISceneEdit).IsAssignableFrom(type))
            .OrderBy(static type => type.Name, StringComparer.Ordinal);

    // Opens the sample scene, or a variant of it, with what the sample's edit needs of it.
    private static async Task<AuthoringFixture> OpenAsync(EditSample sample, SceneData data)
    {
        var fixture = await AuthoringFixture.CreateAsync(data).ConfigureAwait(false);
        sample.Arrange?.Invoke(fixture);
        return fixture;
    }

    private static bool IsForbidden(Type type)
    {
        var plain = Nullable.GetUnderlyingType(type) ?? type;
        return plain.IsArray
            || plain == typeof(Guid)
            || typeof(Scene).IsAssignableFrom(plain)
            || typeof(SceneNode).IsAssignableFrom(plain)
            || typeof(GameComponent).IsAssignableFrom(plain)
            || (plain.IsGenericType && plain.GetGenericTypeDefinition() == typeof(List<>));
    }

    // What a round trip compares: the document and, for edits that only change the editor's view, the hidden nodes.
    private static string StateOf(SceneSnapshot snapshot) => $"{snapshot.Document}|hidden: {snapshot.Hidden}";

    // The order of an edit's own changes is not part of the contract; the order of a batch's inner changes is (3.5).
    private static IReadOnlyList<SceneChange> Normalized(EditSample sample, IReadOnlyList<SceneChange> changes)
        => sample.ChangesInOrder ? changes : [.. changes.OrderBy(static change => change.GetType().Name, StringComparer.Ordinal)];

    // The revert of an edit is reachable only through the scope the frame gives it: apply the edit inside a probe.
    private static async Task<SceneEditApplication.Applied> ApplyInProbeAsync(AuthoringFixture fixture, ISceneEdit edit)
    {
        var applied = new List<SceneEditApplication.Applied>(capacity: 1);
        var probe = new ProbeEdit
        {
            Label = edit.Label,
            Effect = scope =>
            {
                var validation = edit.Validate(scope);
                if (!validation.IsValid)
                {
                    throw new InvalidOperationException($"The edit '{edit.Label}' does not validate: {string.Join(',', validation.Diagnostics.Select(static d => d.Code))}.");
                }

                if (edit.Apply(scope) is SceneEditApplication.Applied result)
                {
                    applied.Add(result);
                }
            },
        };

        _ = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        return applied.Count == 1 ? applied[0] : throw new InvalidOperationException($"The edit '{edit.Label}' did not apply.");
    }
}
