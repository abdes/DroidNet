// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>A material descriptor (<c>*.omat.json</c>).</summary>
public sealed class MaterialDocument : AuthoredDocument<MaterialDocument>
{
    /// <summary>Initializes a new instance of the <see cref="MaterialDocument"/> class.</summary>
    /// <param name="identity">The material's identity.</param>
    public MaterialDocument(AssetPath identity)
        : base(identity, new JsonObject { ["name"] = StemOf(identity) })
    {
    }

    /// <summary>Binds the texture <paramref name="target"/> to <paramref name="slot"/>.</summary>
    /// <param name="slot">The texture slot, such as <c>base_color</c>.</param>
    /// <param name="target">The texture's identity.</param>
    /// <returns>This document.</returns>
    public MaterialDocument Texture(string slot, string target)
        => this.Reference($"/textures/{slot}/virtual_path", target);
}
