// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>An asset offered by a script or asset parameter picker.</summary>
/// <param name="Uri">The asset identity.</param>
/// <param name="Name">The asset name.</param>
/// <param name="Path">The asset's virtual path, shown under its name.</param>
/// <param name="Kind">The asset kind.</param>
public sealed record ScriptAssetChoice(Uri Uri, string Name, string Path, AssetKind Kind)
{
    /// <inheritdoc />
    public override string ToString() => this.Name;
}
