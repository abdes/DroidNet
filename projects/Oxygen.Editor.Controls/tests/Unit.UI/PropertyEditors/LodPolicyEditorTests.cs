// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
/// The Level of detail editor writes the whole policy through its owner on every field edit, a kind from the inherited
/// policy or that kind's defaults, and <see langword="null"/> on reset; its row is labeled by its host and shows only
/// while a policy is available.
/// </summary>
[TestClass]
internal sealed class LodPolicyEditorTests : VisualUserInterfaceTests
{
    private static readonly LodPolicy.Distance Distances = new([5f, 10f, 20f], 0.1f);

    [TestMethod]
    public void AThresholdEditWritesTheWholePolicy()
    {
        var writes = new List<(LodPolicy? Policy, string Label)>();
        var model = CreateModel(writes);
        model.Show(new LodPolicyEditorState(Distances, Distances, LodCount: 4));

        model.Transitions[1].Distance = 12;

        _ = writes.Should().ContainSingle();
        _ = writes[0].Policy.Should().Be(new LodPolicy.Distance([5f, 12f, 20f], 0.1f));
        _ = writes[0].Label.Should().Be("Edit Level of Detail");
    }

    [TestMethod]
    public void ChoosingAKindWritesTheInheritedPolicyOfThatKindOrItsDefaults()
    {
        var writes = new List<(LodPolicy? Policy, string Label)>();
        var model = CreateModel(writes);
        model.Show(new LodPolicyEditorState(new LodPolicy.Fixed(0), Distances, LodCount: 4));

        model.Kind = LodPolicyShapes.DistanceKind;
        model.Kind = LodPolicyShapes.ScreenSpaceErrorKind;

        _ = writes.Select(static write => write.Policy).Should().HaveCount(2);
        _ = writes[0].Policy.Should().Be(Distances);
        _ = writes[1].Policy.Should().BeOfType<LodPolicy.ScreenSpaceError>()
            .Which.EnterFinerSse.Should().HaveCount(3);
    }

    [TestMethod]
    public async Task ResetWritesNoPolicy()
    {
        var writes = new List<(LodPolicy? Policy, string Label)>();
        var model = CreateModel(writes);
        model.Show(new LodPolicyEditorState(Distances, new LodPolicy.Fixed(0), LodCount: 4) { OverrideState = OverrideState.Full });

        await model.ResetCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = writes.Should().Equal((null, "Reset Level of Detail"));
    }

    [TestMethod]
    public void AnOutOfRangeFixedLodIsRefusedWithoutAWrite()
    {
        var writes = new List<(LodPolicy? Policy, string Label)>();
        var model = CreateModel(writes);
        model.Show(new LodPolicyEditorState(new LodPolicy.Fixed(0), Distances, LodCount: 2));

        model.FixedLod = 3;

        _ = writes.Should().BeEmpty();
        _ = model.HasError.Should().BeTrue();
        _ = model.FixedLod.Should().Be(0);
    }

    [TestMethod]
    public Task TheRowCarriesItsHostsLabelAndHidesWithoutAPolicy() => EnqueueAsync(async () =>
    {
        var model = CreateModel([]);
        model.Show(new LodPolicyEditorState(Distances, Distances, LodCount: 4));
        var editor = new LodPolicyEditor { Label = "LOD policy", Model = model };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { editor } }).ConfigureAwait(true);

        _ = editor.FindDescendants().OfType<PropertyCard>().First().PropertyName.Should().Be("LOD policy");
        _ = editor.FindDescendant<StackPanel>()!.Visibility.Should().Be(Visibility.Visible);

        model.Clear();

        _ = editor.FindDescendant<StackPanel>()!.Visibility.Should().Be(Visibility.Collapsed);
    });

    private static LodPolicyEditorModel CreateModel(List<(LodPolicy? Policy, string Label)> writes)
        => new((policy, label) =>
        {
            writes.Add((policy, label));
            return Task.CompletedTask;
        });
}
