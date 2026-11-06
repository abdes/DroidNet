// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>A texture descriptor (<c>*.otex.json</c>).</summary>
public sealed class TextureDocument : AuthoredDocument<TextureDocument>
{
    /// <summary>Initializes a new instance of the <see cref="TextureDocument"/> class.</summary>
    /// <param name="identity">The texture's identity.</param>
    /// <param name="source">The descriptor's <c>source</c> file reference, as written.</param>
    public TextureDocument(AssetPath identity, string source)
        : base(identity, new JsonObject { ["source"] = source })
    {
    }
}
