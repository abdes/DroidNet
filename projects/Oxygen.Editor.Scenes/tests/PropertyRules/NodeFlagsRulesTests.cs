// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The node flags of a node, through <see cref="SetProperties"/>, on <c>Mesh</c>: each flag is written to the node's
/// flags component, undone and redone.
/// </summary>
[TestClass]
public sealed class NodeFlagsRulesTests
{
    [TestMethod]
    [DataRow(0, DisplayName = "IsVisible")]
    [DataRow(1, DisplayName = "CastsShadows")]
    [DataRow(2, DisplayName = "ReceivesShadows")]
    [DataRow(3, DisplayName = "IsStatic")]
    [DataRow(4, DisplayName = "IsRayCastingSelectable")]
    [DataRow(5, DisplayName = "IgnoreParentTransform")]
    public async Task Flag_Toggle_RoundTrips(int flag)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var flags = fixture.Node(SampleScene.Mesh).Flags;
        var (property, current) = flag switch
        {
            0 => (SceneSchema.Node.IsVisible.Id, flags.IsVisible),
            1 => (SceneSchema.Node.CastsShadows.Id, flags.CastsShadows),
            2 => (SceneSchema.Node.ReceivesShadows.Id, flags.ReceivesShadows),
            3 => (SceneSchema.Node.IsStatic.Id, flags.IsStatic),
            4 => (SceneSchema.Node.IsRayCastingSelectable.Id, flags.IsRayCastingSelectable),
            _ => (SceneSchema.Node.IgnoreParentTransform.Id, flags.IgnoreParentTransform),
        };
        var edit = new PropertyEdit.Builder().SetRaw(property, !current).Build();

        _ = await SceneAssert.RoundTripAsync(fixture, new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.Mesh] = edit }, "Toggle flag")).ConfigureAwait(false);
    }
}
