// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The rendering flags of a node, through <see cref="SetProperties"/>, on <c>Mesh</c>.
/// </summary>
[TestClass]
public sealed class NodeRenderingRulesTests
{
    [TestMethod]
    [DataRow(0, DisplayName = "IsVisible")]
    [DataRow(1, DisplayName = "CastsShadows")]
    [DataRow(2, DisplayName = "ReceivesShadows")]
    public async Task Flag_Toggle_RoundTrips(int flag)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var mesh = fixture.Node(SampleScene.Mesh);
        var (property, current) = flag switch
        {
            0 => (SceneProperties.NodeRendering.IsVisible.Id, mesh.IsVisible),
            1 => (SceneProperties.NodeRendering.CastsShadows.Id, mesh.CastsShadows),
            _ => (SceneProperties.NodeRendering.ReceivesShadows.Id, mesh.ReceivesShadows),
        };
        var edit = new PropertyEdit.Builder().SetRaw(property, !current).Build();

        _ = await SceneAssert.RoundTripAsync(fixture, new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.Mesh] = edit }, "Toggle flag")).ConfigureAwait(false);
    }
}
