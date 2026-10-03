// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Inspector.Controls;

/// <summary>Requests a complete linear color edit on the captured inspector owner.</summary>
/// <param name="owner">The lifetime-protected owner supplied by the existing color gesture helper.</param>
/// <param name="color">The display picker value converted to authored linear RGB.</param>
public sealed class InspectorRgbColorPickedEventArgs(IInspectorEditSessionOwner owner, Vector3 color) : EventArgs
{
    /// <summary>Gets the captured edit owner, never a later current selection.</summary>
    public IInspectorEditSessionOwner Owner { get; } = owner;

    /// <summary>Gets the requested authored linear RGB color.</summary>
    public Vector3 Color { get; } = color;
}
