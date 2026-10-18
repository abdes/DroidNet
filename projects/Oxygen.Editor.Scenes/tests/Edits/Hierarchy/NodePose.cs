// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>Reads and sets the transform of a live node, for the tests of the hierarchy and of the node helpers.</summary>
internal static class NodePose
{
    private const float Tolerance = 1e-4f;

    /// <summary>Gets the local transform of a node as data without an identity, so that two reads compare by value.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The local transform.</returns>
    public static TransformData LocalOf(SceneNode node)
    {
        var transform = TransformOf(node);
        return new TransformData
        {
            Id = ComponentId.Empty,
            Name = "Transform",
            Position = transform.LocalPosition,
            Rotation = transform.LocalRotation,
            Scale = transform.LocalScale,
        };
    }

    /// <summary>Gets the world matrix of a node.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The matrix.</returns>
    public static Matrix4x4 WorldOf(SceneNode node) => SceneTransformMath.WorldMatrix(node);

    /// <summary>Gives a node a local transform that is not the identity, as the arrange step of a test.</summary>
    /// <param name="node">The node.</param>
    public static void Pose(SceneNode node)
    {
        var transform = TransformOf(node);
        transform.LocalPosition = new Vector3(3, 4, 5);
        transform.LocalRotation = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, 0.5f);
        transform.LocalScale = new Vector3(1.5f);
    }

    /// <summary>Asserts that two world matrices are the same within a small tolerance on each element.</summary>
    /// <param name="actual">The actual matrix.</param>
    /// <param name="expected">The expected matrix.</param>
    public static void AssertSameWorld(Matrix4x4 actual, Matrix4x4 expected)
    {
        for (var row = 0; row < 4; row++)
        {
            for (var column = 0; column < 4; column++)
            {
                _ = actual[row, column].Should().BeApproximately(expected[row, column], Tolerance, string.Create(CultureInfo.InvariantCulture, $"element [{row},{column}] of the world matrix"));
            }
        }
    }

    private static TransformComponent TransformOf(SceneNode node) => node.Components.OfType<TransformComponent>().Single();
}
