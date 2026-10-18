// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="ScriptValue"/> factory typing, vector component sizing, and input argument rejection.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ScriptValue))]
public sealed class ScriptValueTests
{
    [TestMethod]
    public void Factories_SetKindAndCorrespondingValue()
    {
        var boolVal = ScriptValue.FromBool(value: true);
        _ = boolVal.Kind.Should().Be(ScriptValueKind.BoolValue);
        _ = boolVal.BoolValue.Should().BeTrue();

        var intVal = ScriptValue.FromInt(42);
        _ = intVal.Kind.Should().Be(ScriptValueKind.Int32Value);
        _ = intVal.IntValue.Should().Be(42);

        var floatVal = ScriptValue.FromFloat(3.14f);
        _ = floatVal.Kind.Should().Be(ScriptValueKind.FloatValue);
        _ = floatVal.FloatValue.Should().Be(3.14f);

        var stringVal = ScriptValue.FromString("text");
        _ = stringVal.Kind.Should().Be(ScriptValueKind.StringValue);
        _ = stringVal.StringValue.Should().Be("text");

        var vec2Val = ScriptValue.FromVector(new Vector2(1f, 2f));
        _ = vec2Val.Kind.Should().Be(ScriptValueKind.Vec2Value);
        _ = vec2Val.GetVectorComponents().Should().Equal([1f, 2f]);

        var vec3Val = ScriptValue.FromVector(new Vector3(1f, 2f, 3f));
        _ = vec3Val.Kind.Should().Be(ScriptValueKind.Vec3Value);
        _ = vec3Val.GetVectorComponents().Should().Equal([1f, 2f, 3f]);

        var vec4Val = ScriptValue.FromVector(new Vector4(1f, 2f, 3f, 4f));
        _ = vec4Val.Kind.Should().Be(ScriptValueKind.Vec4Value);
        _ = vec4Val.GetVectorComponents().Should().Equal([1f, 2f, 3f, 4f]);

        var customVec2 = ScriptValue.FromVector(ScriptValueKind.Vec2Value, new Vector4(5f, 6f, 7f, 8f));
        _ = customVec2.Kind.Should().Be(ScriptValueKind.Vec2Value);
        _ = customVec2.GetVectorComponents().Should().Equal([5f, 6f]);

        var assetUri = new Uri("asset:///Content/Scripts/Test.oscript");
        var assetVal = ScriptValue.FromAsset(assetUri);
        _ = assetVal.Kind.Should().Be(ScriptValueKind.AssetValue);
        _ = assetVal.AssetUri.Should().Be(assetUri);

        var nodeId = NodeId.New();
        var nodeVal = ScriptValue.FromNode(nodeId);
        _ = nodeVal.Kind.Should().Be(ScriptValueKind.NodeValue);
        _ = nodeVal.NodeId.Should().Be(nodeId);
    }

    [TestMethod]
    [DataRow(ScriptValueKind.Vec2Value, 2)]
    [DataRow(ScriptValueKind.Vec3Value, 3)]
    [DataRow(ScriptValueKind.Vec4Value, 4)]
    [DataRow(ScriptValueKind.BoolValue, 0)]
    [DataRow(ScriptValueKind.Int32Value, 0)]
    [DataRow(ScriptValueKind.FloatValue, 0)]
    [DataRow(ScriptValueKind.StringValue, 0)]
    [DataRow(ScriptValueKind.AssetValue, 0)]
    [DataRow(ScriptValueKind.NodeValue, 0)]
    public void VectorSize_ReturnsExpectedComponentCount(ScriptValueKind kind, int expectedSize)
        => _ = ScriptValue.VectorSize(kind).Should().Be(expectedSize);

    [TestMethod]
    public void FromString_WithNullValue_ThrowsArgumentNullException()
    {
        Action act = () => ScriptValue.FromString(null!);

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void FromVector_WithNonVectorKind_ThrowsArgumentOutOfRangeException()
    {
        Action act = () => ScriptValue.FromVector(ScriptValueKind.FloatValue, Vector4.Zero);

        _ = act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void FromAsset_WithNullUri_ThrowsArgumentNullException()
    {
        Action act = () => ScriptValue.FromAsset(null!);

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void FromNode_WithEmptyNodeId_ThrowsArgumentException()
    {
        Action act = () => ScriptValue.FromNode(NodeId.Empty);

        _ = act.Should().Throw<ArgumentException>();
    }
}
