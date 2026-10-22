// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The node flags of a node, through <see cref="SetProperties"/>, on <c>Mesh</c>: each flag is written to the node's
/// flags component, undone and redone; visibility and the shadow flags take their source modes, Inherit included, and
/// refuse an undefined one.
/// </summary>
[TestClass]
public sealed class NodeFlagsRulesTests
{
    [TestMethod]
    [DataRow(0, DisplayName = "IsStatic")]
    [DataRow(1, DisplayName = "IsRayCastingSelectable")]
    [DataRow(2, DisplayName = "IgnoreParentTransform")]
    public async Task Flag_Toggle_RoundTrips(int flag)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var flags = fixture.Node(SampleScene.Mesh).Flags;
        var (property, current) = flag switch
        {
            0 => (SceneSchema.Node.IsStatic.Id, flags.IsStatic),
            1 => (SceneSchema.Node.IsRayCastingSelectable.Id, flags.IsRayCastingSelectable),
            _ => (SceneSchema.Node.IgnoreParentTransform.Id, flags.IgnoreParentTransform),
        };
        var edit = new PropertyEdit.Builder().SetRaw(property, !current).Build();

        _ = await SceneAssert.RoundTripAsync(fixture, Edit(edit, "Toggle flag")).ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(VisibilitySource.Inherit)]
    [DataRow(VisibilitySource.Shown)]
    [DataRow(VisibilitySource.Hidden)]
    public async Task Visibility_EachMode_RoundTrips(VisibilitySource mode)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var flags = fixture.Node(SampleScene.Mesh).Flags;
        flags.Visibility = mode == VisibilitySource.Shown ? VisibilitySource.Hidden : VisibilitySource.Shown;
        var edit = new PropertyEdit.Builder().Set(SceneSchema.Node.Visibility, mode).Build();

        _ = await SceneAssert.RoundTripAsync(fixture, Edit(edit, "Set visibility")).ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(true, ShadowSource.Inherit)]
    [DataRow(true, ShadowSource.On)]
    [DataRow(true, ShadowSource.Off)]
    [DataRow(false, ShadowSource.Inherit)]
    [DataRow(false, ShadowSource.On)]
    [DataRow(false, ShadowSource.Off)]
    public async Task Shadows_EachMode_RoundTrips(bool casts, ShadowSource mode)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var flags = fixture.Node(SampleScene.Mesh).Flags;
        var other = mode == ShadowSource.On ? ShadowSource.Off : ShadowSource.On;
        var property = casts ? SceneSchema.Node.CastsShadows : SceneSchema.Node.ReceivesShadows;
        if (casts)
        {
            flags.CastsShadows = other;
        }
        else
        {
            flags.ReceivesShadows = other;
        }

        var edit = new PropertyEdit.Builder().Set(property, mode).Build();

        _ = await SceneAssert.RoundTripAsync(fixture, Edit(edit, "Set shadows")).ConfigureAwait(false);
    }

    [TestMethod]
    public async Task UndefinedMode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.Node.Visibility, (VisibilitySource)3).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit(edit, "Set visibility")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyOutOfRange, SampleScene.Mesh, SceneSchema.Node.Visibility.Id);
        SceneAssert.Unchanged(fixture, before);
    }

    private static SetProperties Edit(PropertyEdit edit, string label)
        => new(new Dictionary<NodeId, PropertyEdit> { [SampleScene.Mesh] = edit }, label);
}
