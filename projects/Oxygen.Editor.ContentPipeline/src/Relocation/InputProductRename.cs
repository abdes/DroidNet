// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Renames an action or a mapping context inside its input source.</summary>
/// <remarks>
/// A product's identity is its source's identity and its name, so a rename gives it a new identity: its source is
/// rewritten, and every reference to it follows, as for a file rename.
/// </remarks>
/// <param name="Product">The product's identity, such as <c>/Content/Input/Player.input/Jump.oiact</c>.</param>
/// <param name="Name">The new name, such as <c>Leap</c>.</param>
public sealed record InputProductRename(AssetPath Product, string Name)
{
    /// <summary>Gets the product's current name.</summary>
    public string CurrentName => this.Product.Name[..^this.GetExtension().Length];

    /// <summary>Gets the product's identity after the rename.</summary>
    public AssetPath Target => this.Product.WithName(this.Name + this.GetExtension());

    /// <summary>Gets the product's cooked extension: <c>.oiact</c> or <c>.oimap</c>.</summary>
    /// <returns>The cooked extension, with its leading dot.</returns>
    /// <exception cref="AssetRelocationException">The product is not a cooked asset.</exception>
    public string GetExtension() => AssetFileTypes.Find(this.Product.Name)?.CookedExtension
        ?? throw new AssetRelocationException($"'{this.Product}' is not an action or a mapping context.");

    /// <summary>Gets the rename that undoes this one.</summary>
    /// <returns>The reverse rename.</returns>
    public InputProductRename Reverse() => new(this.Target, this.CurrentName);
}
