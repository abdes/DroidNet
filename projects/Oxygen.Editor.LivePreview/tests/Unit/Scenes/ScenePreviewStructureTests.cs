// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// The structure of a source comes from the hierarchy the engine reads from the mounted descriptors (scene instances
/// 8.2): every node by its path below the instance root, nested instances included.
/// </summary>
[TestClass]
public sealed class ScenePreviewStructureTests
{
    private const string VirtualPath = "/Content/Prefabs/Outpost.oscene";

    private static readonly AssetPath Source = AssetPath.Parse("/Content/Prefabs/Outpost.oscene");
    private static readonly Guid Outpost = Guid.NewGuid();
    private static readonly Guid Tower = Guid.NewGuid();
    private static readonly Guid Torch = Guid.NewGuid();
    private static readonly NodeId Ground = NodeId.New();
    private static readonly NodeId Watch = NodeId.New();
    private static readonly NodeId Holder = NodeId.New();

    /// <summary>
    /// Outpost owns Ground and Watch; Watch instances Tower, which owns Holder; Holder instances Torch, which is not
    /// mounted. Parents map from indices to paths, each node keeps its defining scene, and the issue names its instance
    /// root by path.
    /// </summary>
    [TestMethod]
    public void TheHierarchyBecomesTheStructureByNodePath()
    {
        var hierarchy = new RuntimeSceneHierarchy(
            Exists: true,
            Outpost,
            [
                new(NodePath.Of(Ground), "Ground", Parent: null, Outpost, InstanceKey: null),
                new(NodePath.Of(Watch), "Watch", Parent: null, Outpost, Tower),
                new(NodePath.Of(Watch, Holder), "Holder", Parent: 1, Tower, Torch),
            ],
            [new(RuntimeSceneHierarchyIssueCode.SceneUnavailable, Node: 2, Torch)]);

        var structure = ScenePreview.ToStructure(Source, VirtualPath, hierarchy);

        _ = structure.Source.Should().Be(Source);
        _ = structure.Scene.Should().Be(new StructureScene(Outpost, VirtualPath));
        _ = structure.ChildrenOf(parent: null).Select(static node => node.Name).Should().Equal("Ground", "Watch");
        var holder = structure.Find(NodePath.Of(Watch, Holder))!;
        _ = holder.Parent.Should().Be(NodePath.Of(Watch), "an instance's roots hang under its instance root");
        _ = holder.DefiningScene.Key.Should().Be(Tower);
        _ = holder.Instance!.Key.Should().Be(Torch);
        _ = structure.Find(NodePath.Of(Watch))!.Instance!.Key.Should().Be(Tower);
        _ = structure.Find(NodePath.Of(Holder)).Should().BeNull("a nested node is named below the source's instance root");
        var issue = structure.Issues.Should().ContainSingle().Subject;
        _ = issue.Code.Should().Be("scene.instance.unavailable");
        _ = issue.Node.Should().Be(NodePath.Of(Watch, Holder));
        _ = issue.Scene.Should().Be(Torch);
    }

    /// <summary>Two nodes with one path are a broken read, not a structure.</summary>
    [TestMethod]
    public void TwoNodesWithOnePathAreRefused()
    {
        var hierarchy = new RuntimeSceneHierarchy(
            Exists: true,
            Outpost,
            [
                new(NodePath.Of(Ground), "Ground", Parent: null, Outpost, InstanceKey: null),
                new(NodePath.Of(Ground), "Again", Parent: null, Outpost, InstanceKey: null),
            ],
            []);

        var read = () => ScenePreview.ToStructure(Source, VirtualPath, hierarchy);

        _ = read.Should().Throw<InvalidDataException>();
    }
}
