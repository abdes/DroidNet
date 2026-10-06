// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Composes scene-node world transforms and computes preserve-world local transforms for reparenting.
/// </summary>
/// <remarks>
/// The composition follows the editor's established convention (see
/// <c>DirectionalLightOrientation.WorldRotation</c>): a child's world transform is its local
/// transform applied first, then its ancestors'. For row-vector matrices this is
/// <c>world = local * parentWorld</c>; a node with <see cref="SceneNode.IgnoreParentTransform"/>
/// set (or without a parent) has <c>world = local</c>. Local matrices are composed as
/// <c>scale * rotation * translation</c>.
/// </remarks>
internal static class SceneTransformMath
{
    private const float Tolerance = 1e-4f;

    /// <summary>Composes the local TRS of a transform component.</summary>
    public static Matrix4x4 LocalMatrix(TransformComponent transform)
        => Matrix4x4.CreateScale(transform.LocalScale)
           * Matrix4x4.CreateFromQuaternion(transform.LocalRotation)
           * Matrix4x4.CreateTranslation(transform.LocalPosition);

    /// <summary>Computes the world transform of a node, honouring <see cref="SceneNode.IgnoreParentTransform"/>.</summary>
    public static Matrix4x4 WorldMatrix(SceneNode node)
    {
        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
        var local = transform is null ? Matrix4x4.Identity : LocalMatrix(transform);
        return node.Parent is null || node.IgnoreParentTransform ? local : local * WorldMatrix(node.Parent);
    }

    /// <summary>
    /// Computes the local TRS that preserves <paramref name="node"/>'s current world pose when it is
    /// reparented under <paramref name="newParent"/> (or to the scene root when <paramref name="newParent"/>
    /// is <see langword="null"/>).
    /// </summary>
    /// <param name="node">The node being reparented.</param>
    /// <param name="newParent">The destination parent, or <see langword="null"/> for the scene root.</param>
    /// <param name="position">The resulting local position.</param>
    /// <param name="rotation">The resulting normalized local rotation.</param>
    /// <param name="scale">The resulting local scale.</param>
    /// <returns>
    /// <see langword="true"/> when the result is representable without shear; otherwise
    /// <see langword="false"/> (singular parent, non-finite values, or unrepresentable shear).
    /// </returns>
    public static bool TryPreserveWorldLocal(
        SceneNode node,
        SceneNode? newParent,
        out Vector3 position,
        out Quaternion rotation,
        out Vector3 scale)
    {
        position = default;
        rotation = default;
        scale = default;

        var world = WorldMatrix(node);

        // A node that ignores its parent has world == local, and it keeps ignoring the
        // destination parent after the move, so its local must stay equal to its world.
        // Dividing by the new parent's world here would shift it (e.g. world X=0 under a
        // parent at X=10 would become X=-10).
        if (node.IgnoreParentTransform)
        {
            return TryDecompose(world, out position, out rotation, out scale);
        }

        var newParentWorld = newParent is null ? Matrix4x4.Identity : WorldMatrix(newParent);
        if (!Matrix4x4.Invert(newParentWorld, out var inverseParent))
        {
            return false;
        }

        var newLocal = world * inverseParent;
        return TryDecompose(newLocal, out position, out rotation, out scale);
    }

    private static bool TryDecompose(Matrix4x4 matrix, out Vector3 position, out Quaternion rotation, out Vector3 scale)
    {
        position = default;
        rotation = default;
        scale = default;

        if (!IsFinite(matrix))
        {
            return false;
        }

        if (!Matrix4x4.Decompose(matrix, out scale, out rotation, out position))
        {
            return false;
        }

        if (!IsFinite(position) || !IsFinite(rotation) || !IsFinite(scale))
        {
            return false;
        }

        if (MathF.Abs(scale.X) < Tolerance || MathF.Abs(scale.Y) < Tolerance || MathF.Abs(scale.Z) < Tolerance)
        {
            return false;
        }

        // Recomposition must reproduce the input within tolerance; a mismatch indicates shear that
        // cannot be represented as a pure TRS local transform.
        var recomposed = Matrix4x4.CreateScale(scale) * Matrix4x4.CreateFromQuaternion(rotation) * Matrix4x4.CreateTranslation(position);
        return ApproximatelyEqual(matrix, recomposed);
    }

    private static bool IsFinite(Matrix4x4 matrix)
        => IsFinite(matrix.M11) && IsFinite(matrix.M12) && IsFinite(matrix.M13) && IsFinite(matrix.M14)
           && IsFinite(matrix.M21) && IsFinite(matrix.M22) && IsFinite(matrix.M23) && IsFinite(matrix.M24)
           && IsFinite(matrix.M31) && IsFinite(matrix.M32) && IsFinite(matrix.M33) && IsFinite(matrix.M34)
           && IsFinite(matrix.M41) && IsFinite(matrix.M42) && IsFinite(matrix.M43) && IsFinite(matrix.M44);

    private static bool IsFinite(Vector3 value)
        => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static bool IsFinite(Quaternion value)
        => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) && float.IsFinite(value.W);

    private static bool IsFinite(float value) => float.IsFinite(value);

    private static bool ApproximatelyEqual(Matrix4x4 left, Matrix4x4 right)
        => MathF.Abs(left.M11 - right.M11) <= Tolerance && MathF.Abs(left.M12 - right.M12) <= Tolerance
           && MathF.Abs(left.M13 - right.M13) <= Tolerance && MathF.Abs(left.M14 - right.M14) <= Tolerance
           && MathF.Abs(left.M21 - right.M21) <= Tolerance && MathF.Abs(left.M22 - right.M22) <= Tolerance
           && MathF.Abs(left.M23 - right.M23) <= Tolerance && MathF.Abs(left.M24 - right.M24) <= Tolerance
           && MathF.Abs(left.M31 - right.M31) <= Tolerance && MathF.Abs(left.M32 - right.M32) <= Tolerance
           && MathF.Abs(left.M33 - right.M33) <= Tolerance && MathF.Abs(left.M34 - right.M34) <= Tolerance
           && MathF.Abs(left.M41 - right.M41) <= Tolerance && MathF.Abs(left.M42 - right.M42) <= Tolerance
           && MathF.Abs(left.M43 - right.M43) <= Tolerance && MathF.Abs(left.M44 - right.M44) <= Tolerance;
}
