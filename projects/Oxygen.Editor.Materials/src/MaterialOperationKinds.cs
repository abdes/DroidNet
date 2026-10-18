// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Materials;

/// <summary>
/// The operation kinds of material authoring, in the operation results it publishes.
/// </summary>
public static class MaterialOperationKinds
{
    /// <summary>A new material is created.</summary>
    public const string Create = "Material.Create";

    /// <summary>A material is edited.</summary>
    public const string EditScalar = "Material.EditScalar";
}
