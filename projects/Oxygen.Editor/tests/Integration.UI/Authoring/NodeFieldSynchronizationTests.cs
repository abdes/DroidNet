// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using static Oxygen.Editor.TestSupport.NativeFieldCases;
using static Oxygen.Editor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.Integration.UI.Tests.Authoring;

[TestClass]
public sealed partial class NodeFieldSynchronizationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>An edit of one field per value kind and conversion reaches the engine, and history and Save/reopen keep the authored and native values together.</summary>
    /// <param name="kind">The seeded node kind.</param>
    /// <param name="fieldName">The numeric, boolean or enum field.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Transform", "PositionX")]
    [DataRow("Transform", "RotationY")]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "ColorR")]
    [DataRow("Light", "CastsShadows")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Light", "CascadeCount")]
    [DataRow("Light", "SplitMode")]
    [DataRow("Node", "Static")]
    [DataRow("Node", "RayCastingSelectable")]
    [DataRow("Node", "IgnoreParentTransform")]
    public Task NodeFieldEditHistoryAndReopenReachNativeState(string kind, string fieldName) => EnqueueAsync(async () =>
    {
        var field = NativeNodeFields.Single(value => string.Equals(value.Kind, kind, StringComparison.Ordinal) && string.Equals(value.Field, fieldName, StringComparison.Ordinal));
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, kind));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        var before = SourceNodeProperties(node);
        await AssertNodeValuesAsync(fixture, node.Id, before, timeout.Token).ConfigureAwait(true);

        var edit = new SetProperties(new Dictionary<NodeId, PropertyEdit> { [node.Id] = field.Edit(node) }, $"Edit {field.Field}");
        _ = (await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        var expected = new Dictionary<(ushort component, ushort field), float>(before)
        {
            [(field.Component, field.NativeField)] = field.ExpectedValue,
        };
        await AssertNodeValuesAsync(fixture, node.Id, expected, timeout.Token).ConfigureAwait(true);
        await fixture.Document.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, before, timeout.Token).ConfigureAwait(true);
        await fixture.Document.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>A manual split keeps its stored fourth cascade boundary in the engine and through Save/reopen, though three cascades use only three boundaries.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task StoredFourthCascadeBoundarySurvivesReopen() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, static scene => SeedNativeNode(scene, "Light", static node =>
        {
            var light = node.Components.OfType<DirectionalLightComponent>().Single();
            light.SplitMode = DirectionalCsmSplitMode.ManualDistances;
            light.MaxShadowDistance = 240f;
            light.CascadeDistances = new Vector4(8f, 24f, 64f, 220f);
        }));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        var expected = SourceNodeProperties(node);
        _ = expected[(3, 21)].Should().Be(220f);
        await AssertNodeValuesAsync(fixture, node.Id, expected, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });

    private static async Task AssertNodeValuesAsync(NativeSceneFixture fixture, NodeId nodeId, Dictionary<(ushort component, ushort field), float> expected, CancellationToken cancellationToken)
    {
        var source = SourceNodeProperties(fixture.Source.RootNodes.Single(node => node.Id == nodeId));
        var native = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        _ = native.Exists.Should().BeTrue();
        _ = native.Properties.Should().HaveCount(expected.Count);
        foreach (var (key, value) in expected)
        {
            _ = source[key].Should().BeApproximately(value, 0.0001f, "source component {0}, field {1}", key.component, key.field);
            var actual = native.Properties.Single(property => (ushort)property.ComponentId == key.component && property.FieldId == key.field);
            _ = actual.Value.Should().BeApproximately(value, 0.0001f, "native component {0}, field {1}", key.component, key.field);
        }
    }
}
