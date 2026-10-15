// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>An entry of the starting camera list: Automatic, a camera, or the authored camera that is gone.</summary>
/// <remarks>Entries compare by identity, so the list keeps its entries while the cameras are unchanged and a
/// selection set to an entry always reaches the list.</remarks>
/// <param name="id">The camera node; null for Automatic.</param>
/// <param name="name">The text the list shows.</param>
public sealed class StartingCameraChoice(Guid? id, string name)
{
    /// <summary>Gets the camera node; null for Automatic.</summary>
    public Guid? Id { get; } = id;

    /// <summary>Gets the text the list shows.</summary>
    public string Name { get; } = name;

    /// <inheritdoc />
    public override string ToString() => this.Name;
}
