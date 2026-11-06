// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>
/// The one writer of each authored format in the editor's tests.
/// </summary>
/// <remarks>
/// A test names what a document must say, never how the contract spells it: a document knows its own identity, and a
/// reference names its target's identity. <see cref="SpellReference"/> turns a target into the text the contract writes, so
/// a change of the reference contract changes that method and the identity fields below, not the tests. A test that checks
/// a spelling itself (a reader refusal) writes the text with <see cref="AuthoredDocument{TSelf}.Set"/>.
/// </remarks>
public static class AuthoredFiles
{
    /// <summary>Gets the text an authored file whose identity is <paramref name="anchor"/> writes to name <paramref name="target"/>.</summary>
    /// <param name="anchor">The identity of the file that writes the reference.</param>
    /// <param name="target">The identity the reference names.</param>
    /// <returns>The reference, as every writer spells it.</returns>
    public static string SpellReference(AssetPath anchor, AssetPath target)
    {
        ArgumentNullException.ThrowIfNull(anchor);
        ArgumentNullException.ThrowIfNull(target);
        return target.ToReference(anchor);
    }

    /// <summary>Starts a material descriptor (<c>*.omat.json</c>).</summary>
    /// <param name="identity">The material's identity, such as <c>/Content/Materials/Wall.omat</c>.</param>
    /// <returns>A descriptor naming the material after its identity.</returns>
    public static MaterialDocument Material(string identity) => new(AssetPath.Parse(identity));

    /// <summary>Starts a texture descriptor (<c>*.otex.json</c>).</summary>
    /// <param name="identity">The texture's identity, such as <c>/Content/Textures/Wood.otex</c>.</param>
    /// <param name="source">The descriptor's <c>source</c> file reference, as written.</param>
    /// <returns>A descriptor with only its source.</returns>
    public static TextureDocument Texture(string identity, string source) => new(AssetPath.Parse(identity), source);

    /// <summary>Starts a geometry descriptor (<c>*.ogeo.json</c>).</summary>
    /// <param name="identity">The geometry's identity, such as <c>/Content/Geometry/Crate.ogeo</c>.</param>
    /// <returns>A descriptor with one procedural LOD whose one submesh uses the engine's default material.</returns>
    public static GeometryDocument Geometry(string identity) => new(AssetPath.Parse(identity));

    /// <summary>Starts an input source (<c>*.input.json</c>).</summary>
    /// <param name="identity">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <returns>A source with no actions and no contexts.</returns>
    public static InputDocument Input(string identity) => new(AssetPath.Parse(identity));
}
