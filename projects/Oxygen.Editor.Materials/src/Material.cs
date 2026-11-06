// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials;

/// <summary>
/// A material being authored: its current <see cref="MaterialSource"/>, replaced whole on each change, and the rules
/// of materials that hold whatever shows it.
/// </summary>
/// <remarks>
/// The source is immutable, so a history keeps earlier sources as snapshots and restores one with
/// <see cref="Replace"/>. A material holds its textures by identity; its file spells them as references from its own
/// identity, the form of the engine's descriptor.
/// </remarks>
public sealed class Material
{
    /// <summary>Initializes a new instance of the <see cref="Material"/> class.</summary>
    /// <param name="source">The material's source.</param>
    /// <param name="identity">The material's identity.</param>
    public Material(MaterialSource source, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(identity);
        this.Source = source;
        this.Identity = identity;
    }

    /// <summary>Occurs on the thread that replaced <see cref="Source"/>, after it changed.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the material's current source.</summary>
    public MaterialSource Source { get; private set; }

    /// <summary>Gets the material's identity, which its file's references are spelled from.</summary>
    public AssetPath Identity { get; private set; }

    /// <summary>Gets the code of the problem with a texture assignment, or <see langword="null"/> when it is valid.</summary>
    /// <param name="channel">The texture channel.</param>
    /// <returns><see cref="MaterialDiagnosticCodes.TextureChannelInvalid"/> or <see langword="null"/>.</returns>
    public static string? CheckTexture(string? channel)
        => MaterialSource.IsTextureChannel(channel) ? null : MaterialDiagnosticCodes.TextureChannelInvalid;

    /// <summary>Returns a source whose textures follow a relocation.</summary>
    /// <param name="source">The source to retarget, such as a history snapshot.</param>
    /// <param name="map">The new identity of a texture, or <see langword="null"/> when it did not move.</param>
    /// <returns><paramref name="source"/> when no texture moved; otherwise the retargeted copy.</returns>
    public static MaterialSource Retarget(MaterialSource source, Func<AssetPath, AssetPath?> map)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(map);
        foreach (var (channel, texture) in source.TextureReferences)
        {
            if (map(texture) is { } mapped && mapped != texture)
            {
                source = source.WithTextureReference(channel, mapped);
            }
        }

        return source;
    }

    /// <summary>Gets a value indicating whether a source writes the same file as this material.</summary>
    /// <param name="other">The other source.</param>
    /// <returns><see langword="true"/> when saving either writes the same bytes.</returns>
    public bool IsSameAs(MaterialSource other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ReferenceEquals(this.Source, other) || Serialize(this.Source, this.Identity).AsSpan().SequenceEqual(Serialize(other, this.Identity));
    }

    /// <summary>Gives the material the identity its file now has, which respells its references when it is next saved.</summary>
    /// <param name="identity">The new identity.</param>
    public void MoveTo(AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        this.Identity = identity;
    }

    /// <summary>Replaces the source, and raises <see cref="Changed"/> when it is a different one.</summary>
    /// <param name="source">The new source.</param>
    public void Replace(MaterialSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (ReferenceEquals(this.Source, source))
        {
            return;
        }

        this.Source = source;
        this.Changed?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Writes a source as its file content.</summary>
    /// <param name="source">The source.</param>
    /// <param name="identity">The identity of the material the file holds.</param>
    /// <returns>The UTF-8 file content.</returns>
    /// <exception cref="Oxygen.Managed.Assets.Diagnostics.AssetFormatException">The source does not make a valid descriptor.</exception>
    internal static byte[] Serialize(MaterialSource source, AssetPath identity)
    {
        using var memory = new MemoryStream();
        MaterialSourceWriter.Write(memory, source, identity);
        return memory.ToArray();
    }
}
