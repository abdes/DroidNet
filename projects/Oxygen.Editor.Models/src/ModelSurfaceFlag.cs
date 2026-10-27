// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>A surface default a model's import writes into its geometry.</summary>
public enum ModelSurfaceFlag
{
    /// <summary>Whether the surface renders.</summary>
    Visible,

    /// <summary>Whether the surface casts shadows.</summary>
    CastsShadows,
}
