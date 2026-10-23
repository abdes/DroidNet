// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// The Transform controls take what a value may be from the generated schema range of the axis they edit: scale is
/// nonzero, rotation is any finite angle (the display wraps it), and a position is finite.
/// </summary>
[TestClass]
[TestCategory("Inspector")]
internal sealed class TransformRangeTests
{
    [TestMethod]
    [DataRow(TransformEditFieldGroup.Scale, 0.0, RangeCheck.OutOfRange)]
    [DataRow(TransformEditFieldGroup.Scale, 0.0005, RangeCheck.InRange)]
    [DataRow(TransformEditFieldGroup.Scale, -2.0, RangeCheck.InRange)]
    [DataRow(TransformEditFieldGroup.Scale, double.NaN, RangeCheck.NotFinite)]
    [DataRow(TransformEditFieldGroup.Rotation, 270.0, RangeCheck.InRange)]
    [DataRow(TransformEditFieldGroup.Rotation, -720.0, RangeCheck.InRange)]
    [DataRow(TransformEditFieldGroup.Rotation, double.PositiveInfinity, RangeCheck.NotFinite)]
    [DataRow(TransformEditFieldGroup.Position, 1.0e6, RangeCheck.InRange)]
    [DataRow(TransformEditFieldGroup.Position, double.NegativeInfinity, RangeCheck.NotFinite)]
    public void CheckValue_FollowsTheSchemaRangeOfEveryComponent(TransformEditFieldGroup group, double value, RangeCheck expected)
    {
        foreach (var component in new[] { Component.X, Component.Y, Component.Z })
        {
            _ = TransformEditController.CheckValue(group, component, value).Should().Be(expected);
        }
    }
}
