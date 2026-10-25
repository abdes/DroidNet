// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

/// <summary>
/// The structure of a source is the engine's composed report of its cooked scene (scene instances 8.2): every node by
/// node path below the instance root, nested instances included, with each value's supplying scene.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SceneStructureTests
{
    private const string Source = "11111111-1111-1111-1111-111111111111";
    private const string Tower = "22222222-2222-2222-2222-222222222222";
    private const string Torch = "33333333-3333-3333-3333-333333333333";
    private const string Ground = "a0000000-0000-0000-0000-00000000000a";
    private const string Watch = "b0000000-0000-0000-0000-00000000000b";
    private const string Holder = "c0000000-0000-0000-0000-00000000000c";
    private const string Flame = "d0000000-0000-0000-0000-00000000000d";
    private const string Mesh = "e0000000-0000-0000-0000-00000000000e";
    private const string Surface = "f0000000-0000-0000-0000-00000000000f";
    private const string Slot = "99999999-9999-9999-9999-999999999999";

    private static readonly AssetPath SourceUri = AssetPath.Parse("/Content/Prefabs/Outpost.oscene");

    /// <summary>
    /// Two nesting levels: Outpost owns Ground and Watch, Watch instances Tower whose Holder instances Torch. Each node's
    /// path is Outpost's node, then one ID per level; its parent, defining scene and nested source follow the report.
    /// </summary>
    [TestMethod]
    public void ANestedSceneNamesEveryNodeByItsPathBelowTheInstanceRoot()
    {
        var structure = CookedSceneReport.Parse(Report().ToJsonString()).SingleOrDefault(SourceUri)!;

        _ = structure.Source.Should().Be(SourceUri);
        _ = structure.Scene.Should().Be(new StructureScene(Guid.Parse(Source), "/Content/Prefabs/Outpost.oscene"));
        _ = structure.Nodes.Select(static node => node.Path).Should().Equal(
            PathOf(Ground), PathOf(Watch), PathOf(Watch, Holder), PathOf(Watch, Holder, Flame));
        var flame = structure.Find(PathOf(Watch, Holder, Flame))!;
        _ = flame.Name.Should().Be("Flame");
        _ = flame.Parent.Should().Be(PathOf(Watch, Holder));
        _ = flame.DefiningScene.Should().Be(new StructureScene(Guid.Parse(Torch), "/Content/Prefabs/Torch.oscene"));
        _ = structure.Find(PathOf(Ground))!.Parent.Should().BeNull("a root node of the source has no parent");
        _ = structure.Find(PathOf(Watch, Holder))!.Parent.Should().Be(PathOf(Watch), "an instance's roots hang under its instance root");
        _ = structure.Find(PathOf(Watch))!.Instance.Should().Be(new StructureScene(Guid.Parse(Tower), "/Content/Prefabs/Tower.oscene"));
        _ = structure.Find(PathOf(Watch, Holder))!.Instance!.Key.Should().Be(Guid.Parse(Torch));
        _ = structure.Find(PathOf(Holder)).Should().BeNull("a nested node is named below the source's instance root");
        _ = structure.ChildrenOf(parent: null).Select(static node => node.Path).Should().Equal(PathOf(Ground), PathOf(Watch));
        _ = structure.ChildrenOf(PathOf(Watch)).Select(static node => node.Path).Should().Equal(PathOf(Watch, Holder));
    }

    /// <summary>Each value keeps the scene that supplies it, and maps to the editor's property identity.</summary>
    [TestMethod]
    public void EveryValueKeepsItsPropertyAndSupplyingScene()
    {
        var structure = CookedSceneReport.Parse(Report().ToJsonString()).SingleOrDefault(SourceUri)!;

        var holder = structure.Find(PathOf(Watch, Holder))!;
        var position = holder.Properties.Should().ContainSingle().Subject;
        _ = position.Property.Should().Be(SceneSchema.Transform.PositionY.Id);
        _ = position.Part.Should().BeNull();
        _ = position.Value.GetSingle().Should().Be(4.5f);
        _ = position.Supplier.Key.Should().Be(Guid.Parse(Source), "Outpost changes the height of Tower's holder");

        var flame = structure.Find(PathOf(Watch, Holder, Flame))!;
        _ = flame.Geometry.Should().Be(Guid.Parse(Mesh));
        var visible = flame.Properties.Should().ContainSingle().Subject;
        _ = visible.Property.Should().Be(SceneSchema.Geometry.Surface.Visibility.Id);
        _ = visible.Part.Should().Be(new PartId(SceneSchema.Geometry.Surface.Kind, Guid.Parse(Surface)));
        _ = visible.Value.GetBoolean().Should().BeFalse();
        _ = visible.Supplier.Key.Should().Be(Guid.Parse(Tower));
        var script = flame.Scripts.Should().ContainSingle().Subject;
        _ = script.SlotId.Should().Be(Guid.Parse(Slot));
        _ = script.ExecutionOrder.Should().Be(2);
        var parameter = script.Parameters.Should().ContainSingle().Subject;
        _ = parameter.Key.Should().Be("speed");
        _ = parameter.Value.GetDouble().Should().Be(3);
        _ = parameter.Supplier.Key.Should().Be(Guid.Parse(Torch));
        _ = flame.Components.Should().Equal(SceneSchema.Transform.Kind, SceneSchema.Geometry.Kind, SceneSchema.Scripting.Kind);
        _ = structure.Find(PathOf(Watch))!.Components.Should().Contain(SceneSchema.SceneInstance.Kind);
    }

    /// <summary>The changes that resolve to nothing and the composition's issues reach the structure.</summary>
    [TestMethod]
    public void UnresolvedChangesAndIssuesAreKept()
    {
        var report = Report();
        var scene = report["scenes"]![0]!;
        scene["unresolved"] = new JsonArray(
            JsonNode.Parse($$"""{"supplier": 0, "instance_root": 1, "node_path": ["{{Holder}}", "{{Ground}}"], "property": {"override_id": 6, "key": "/local_position/1", "part_id": null} }"""),
            JsonNode.Parse($$"""{"supplier": 1, "instance_root": 2, "node_path": ["{{Flame}}"], "parameter": {"slot_id": "{{Slot}}", "key": "gone"} }"""));
        scene["issues"] = new JsonArray(JsonNode.Parse($$"""{"code": "scene.instance.missing", "occurrence": 1, "node": 2, "scene": "{{Torch}}"}"""));

        var structure = CookedSceneReport.Parse(report.ToJsonString()).SingleOrDefault(SourceUri)!;

        _ = structure.Unresolved.Should().HaveCount(2);
        var property = structure.Unresolved[0];
        _ = property.Supplier.Key.Should().Be(Guid.Parse(Source));
        _ = property.InstanceRoot.Should().Be(PathOf(Watch));
        _ = property.Path.Should().Be(PathOf(Holder, Ground));
        _ = property.Property.Should().Be(new UnresolvedProperty(6, "/local_position/1", Part: null));
        _ = property.Parameter.Should().BeNull();
        var parameter = structure.Unresolved[1];
        _ = parameter.InstanceRoot.Should().Be(PathOf(Watch, Holder));
        _ = parameter.Parameter.Should().Be(new UnresolvedParameter(Guid.Parse(Slot), "gone"));
        var issue = structure.Issues.Should().ContainSingle().Subject;
        _ = issue.Should().Be(new StructureIssue("scene.instance.missing", new StructureScene(Guid.Parse(Tower), "/Content/Prefabs/Tower.oscene"), PathOf(Watch, Holder), Guid.Parse(Torch)));
    }

    /// <summary>A root without the scene has no structure.</summary>
    [TestMethod]
    public void AReportWithoutTheSceneHasNoStructure()
    {
        var report = Report();
        report["scenes"] = new JsonArray();

        _ = CookedSceneReport.Parse(report.ToJsonString()).SingleOrDefault(SourceUri).Should().BeNull();
    }

    /// <summary>A scene the engine could not compose is a failure of the read, not an empty structure.</summary>
    [TestMethod]
    public void ASceneTheEngineCouldNotComposeIsRefused()
    {
        var report = Report();
        var scene = report["scenes"]![0]!;
        scene["complete"] = false;
        scene["diagnostic"] = "The scene is not in its root.";
        report["complete"] = false;
        var parsed = CookedSceneReport.Parse(report.ToJsonString());

        Action read = () => parsed.SingleOrDefault(SourceUri);

        _ = read.Should().Throw<InvalidDataException>();
    }

    /// <summary>A report outside the schema, such as the earlier version, is refused.</summary>
    [TestMethod]
    public void AReportOutsideTheSchemaIsRefused()
    {
        var report = Report();
        report["schema"] = "oxygen.cooked-scenes.v1";

        Action parse = () => CookedSceneReport.Parse(report.ToJsonString());

        _ = parse.Should().Throw<InvalidDataException>();
    }

    /// <summary>A report that contradicts itself never reaches the views.</summary>
    /// <param name="defect">The defect to put in the report.</param>
    [TestMethod]
    [DataRow("path-elsewhere")]
    [DataRow("parent-missing")]
    [DataRow("supplier-missing")]
    [DataRow("duplicate-path")]
    [DataRow("unknown-property")]
    public void AContradictoryReportIsRefused(string defect)
    {
        var report = Report();
        var nodes = report["scenes"]![0]!["nodes"]!.AsArray();
        switch (defect)
        {
            case "path-elsewhere":
                nodes[3]!["node_path"] = new JsonArray(Holder, Holder);
                break;
            case "parent-missing":
                nodes[3]!["parent"] = 9;
                break;
            case "supplier-missing":
                nodes[2]!["components"]![0]!["properties"]![0]!["supplier"] = 7;
                break;
            case "duplicate-path":
                nodes.Add(nodes[0]!.DeepClone());
                break;
            default:
                nodes[2]!["components"]![0]!["properties"]![0]!["key"] = "/not_a_property";
                break;
        }

        var parsed = CookedSceneReport.Parse(report.ToJsonString());

        Action read = () => parsed.SingleOrDefault(SourceUri);

        _ = read.Should().Throw<InvalidDataException>();
    }

    private static NodePath PathOf(params string[] ids) => NodePath.Of(ids.Select(static id => NodeId.FromValue(Guid.Parse(id))));

    // Outpost (Ground, Watch -> Tower), Tower (Holder -> Torch), Torch (Flame), composed as the engine reports it.
    private static JsonNode Report() => JsonNode.Parse($$"""
        {
          "schema": "oxygen.cooked-scenes.v2",
          "source_key": "44444444-4444-4444-4444-444444444444",
          "complete": true,
          "scenes": [{
            "asset_key": "{{Source}}",
            "virtual_path": "/Content/Prefabs/Outpost.oscene",
            "descriptor_version": 7,
            "complete": true,
            "occurrences": [
              {"scene": "{{Source}}", "virtual_path": "/Content/Prefabs/Outpost.oscene", "parent": null, "instance_root": null},
              {"scene": "{{Tower}}", "virtual_path": "/Content/Prefabs/Tower.oscene", "parent": 0, "instance_root": 1},
              {"scene": "{{Torch}}", "virtual_path": "/Content/Prefabs/Torch.oscene", "parent": 1, "instance_root": 0}
            ],
            "nodes": [
              {"occurrence": 0, "index": 0, "node_id": "{{Ground}}", "root": "{{Ground}}", "node_path": [], "name": "Ground",
               "parent": null, "defining_scene": "{{Source}}", "components": [], "scripts": []},
              {"occurrence": 0, "index": 1, "node_id": "{{Watch}}", "root": "{{Watch}}", "node_path": [], "name": "Watch",
               "parent": null, "defining_scene": "{{Source}}", "instance": {"source": "{{Tower}}"}, "components": [], "scripts": []},
              {"occurrence": 1, "index": 0, "node_id": "{{Holder}}", "root": "{{Watch}}", "node_path": ["{{Holder}}"], "name": "Holder",
               "parent": 1, "defining_scene": "{{Tower}}", "instance": {"source": "{{Torch}}"},
               "components": [{"kind": "transform", "properties": [{"key": "/local_position/1", "override_id": 6, "value": 4.5, "supplier": 0}]}],
               "scripts": []},
              {"occurrence": 2, "index": 0, "node_id": "{{Flame}}", "root": "{{Watch}}", "node_path": ["{{Holder}}", "{{Flame}}"], "name": "Flame",
               "parent": 2, "defining_scene": "{{Torch}}", "components": [{"kind": "transform", "properties": []}],
               "geometry": {"asset_key": "{{Mesh}}", "overrides": [{"key": "/visible", "override_id": 2, "part_id": "{{Surface}}", "value": false, "supplier": 1}]},
               "scripts": [{"slot_id": "{{Slot}}", "script": "55555555-5555-5555-5555-555555555555", "execution_order": 2,
                 "parameters": [{"key": "speed", "value": 3, "supplier": 2}]}]}
            ],
            "unresolved": [],
            "issues": []
          }]
        }
        """)!;
}
