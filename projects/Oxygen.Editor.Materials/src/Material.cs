// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.Materials;

/// <summary>
/// A material being authored: its current <see cref="MaterialSource"/>, replaced whole on each change, and the rules
/// of materials that hold whatever shows it.
/// </summary>
/// <remarks>
/// The source is immutable, so a history keeps earlier sources as snapshots and restores one with
/// <see cref="Replace"/>. A material references its textures by virtual path, the form of the engine's descriptor.
/// </remarks>
public sealed class Material
{
    /// <summary>Initializes a new instance of the <see cref="Material"/> class.</summary>
    /// <param name="source">The material's source.</param>
    public Material(MaterialSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.Source = source;
    }

    /// <summary>Occurs on the thread that replaced <see cref="Source"/>, after it changed.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the material's current source.</summary>
    public MaterialSource Source { get; private set; }

    /// <summary>Gets the code of the problem with a texture assignment, or <see langword="null"/> when it is valid.</summary>
    /// <param name="channel">The texture channel.</param>
    /// <param name="virtualPath">The texture's path, or <see langword="null"/> to clear the channel.</param>
    /// <returns><see cref="MaterialDiagnosticCodes.TextureChannelInvalid"/>, <see cref="MaterialDiagnosticCodes.TexturePathInvalid"/> or <see langword="null"/>.</returns>
    public static string? CheckTexture(string? channel, string? virtualPath)
        => !MaterialSource.IsTextureChannel(channel) ? MaterialDiagnosticCodes.TextureChannelInvalid
            : virtualPath is not null && !MaterialSource.IsTexturePath(virtualPath) ? MaterialDiagnosticCodes.TexturePathInvalid
            : null;

    /// <summary>Returns a source whose textures follow a relocation.</summary>
    /// <param name="source">The source to retarget, such as a history snapshot.</param>
    /// <param name="map">The new path of a texture, or <see langword="null"/> when it did not move.</param>
    /// <returns><paramref name="source"/> when no texture moved; otherwise the retargeted copy.</returns>
    public static MaterialSource Retarget(MaterialSource source, Func<string, string?> map)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(map);
        foreach (var (channel, path) in source.TextureReferences)
        {
            if (map(path) is { } mapped && !string.Equals(mapped, path, StringComparison.Ordinal))
            {
                source = source.WithTextureReference(channel, mapped);
            }
        }

        return source;
    }

    /// <summary>Gets a value indicating whether two sources write the same file.</summary>
    /// <param name="first">The first source.</param>
    /// <param name="second">The second source.</param>
    /// <returns><see langword="true"/> when saving either writes the same bytes.</returns>
    public static bool AreSame(MaterialSource first, MaterialSource second)
    {
        ArgumentNullException.ThrowIfNull(first);
        ArgumentNullException.ThrowIfNull(second);
        return ReferenceEquals(first, second) || Serialize(first).AsSpan().SequenceEqual(Serialize(second));
    }

    /// <summary>Gets a value indicating whether a source writes the same file as this material.</summary>
    /// <param name="other">The other source.</param>
    /// <returns><see langword="true"/> when saving either writes the same bytes.</returns>
    public bool IsSameAs(MaterialSource other) => AreSame(this.Source, other);

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
    /// <returns>The UTF-8 file content.</returns>
    /// <exception cref="Oxygen.Managed.Assets.Diagnostics.AssetFormatException">The source does not make a valid descriptor.</exception>
    internal static byte[] Serialize(MaterialSource source)
    {
        using var memory = new MemoryStream();
        MaterialSourceWriter.Write(memory, source);
        return memory.ToArray();
    }
}
