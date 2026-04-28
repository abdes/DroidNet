// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable material operation-kind names used by editor operation results.
/// </summary>
public static class MaterialOperationKinds
{
    /// <summary>
    /// Material document creation.
    /// </summary>
    public const string Create = "Material.Create";

    /// <summary>
    /// Material document open.
    /// </summary>
    public const string Open = "Material.Open";

    /// <summary>
    /// Scalar material field edit.
    /// </summary>
    public const string EditScalar = "Material.EditScalar";

    /// <summary>
    /// Material document save.
    /// </summary>
    public const string Save = "Material.Save";

    /// <summary>
    /// Material source cook.
    /// </summary>
    public const string Cook = "Material.Cook";

    /// <summary>
    /// Geometry material assignment.
    /// </summary>
    public const string AssignToGeometry = "Material.AssignToGeometry";

    /// <summary>
    /// Material picker selection.
    /// </summary>
    public const string Pick = "Material.Pick";
}
