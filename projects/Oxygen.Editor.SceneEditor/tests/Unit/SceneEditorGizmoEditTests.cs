// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

/// <summary>
/// The property edits a gizmo or light helper drag submits through the scene editor view model.
/// </summary>
[TestClass]
[TestCategory("Viewport Tools")]
internal sealed class SceneEditorGizmoEditTests
{
    private static readonly NodeReference TargetNode = NodeReference.To(NodeId.New());

    [TestMethod]
    public void GizmoEdits_ChangeOnlyWhatTheToolMoves()
    {
        var target = new RuntimeGizmoTarget(TargetNode, new Vector3(1, 2, 3), Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI / 2), new Vector3(2, 3, 4));

        var move = SceneEditorViewModel.BuildGizmoEdits(RuntimeTransformTool.Translate, [target])[TargetNode];
        var rotate = SceneEditorViewModel.BuildGizmoEdits(RuntimeTransformTool.Rotate, [target])[TargetNode];
        var scale = SceneEditorViewModel.BuildGizmoEdits(RuntimeTransformTool.Scale, [target])[TargetNode];

        _ = move.Ids.Should().BeEquivalentTo([SceneSchema.Transform.PositionX.Id, SceneSchema.Transform.PositionY.Id, SceneSchema.Transform.PositionZ.Id]);
        _ = rotate.Count.Should().Be(4);
        _ = rotate.GetTyped(SceneSchema.Transform.Rotation, out var degrees).Should().BeTrue();
        _ = degrees.Z.Should().BeApproximately(90.0F, 1.0e-3F);
        _ = scale.GetTyped(SceneSchema.Transform.ScaleY, out var scaleY).Should().BeTrue();
        _ = scaleY.Should().Be(3.0F);
        _ = scale.Contains(SceneSchema.Transform.Rotation.Id).Should().BeFalse();
    }

    [TestMethod]
    public void HelperEdits_ChangeOnlyTheDraggedLightValue()
    {
        var outer = SceneEditorViewModel.BuildHelperEdit(RuntimeHelperLight.Spot, RuntimeHelperHandle.OuterCone, 0.6F);
        var range = SceneEditorViewModel.BuildHelperEdit(RuntimeHelperLight.Point, RuntimeHelperHandle.Range, 7.5F);

        _ = outer!.Ids.Should().BeEquivalentTo([SceneProperties.SpotLight.OuterConeAngleRadiansDescriptor!.Id]);
        _ = outer.GetTyped(new PropertyId<float>(SceneProperties.SpotLight.OuterConeAngleRadiansDescriptor!.Id), out var radians).Should().BeTrue();
        _ = radians.Should().Be(0.6F);
        _ = range!.Ids.Should().BeEquivalentTo([SceneProperties.PointLight.RangeDescriptor.Id]);
        _ = SceneEditorViewModel.BuildHelperEdit(RuntimeHelperLight.Point, RuntimeHelperHandle.InnerCone, 0.2F).Should().BeNull();
        _ = SceneEditorViewModel.BuildHelperEdit(RuntimeHelperLight.None, RuntimeHelperHandle.Range, 1.0F).Should().BeNull();
    }
}
