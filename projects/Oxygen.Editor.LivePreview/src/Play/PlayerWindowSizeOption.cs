// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>An entry of the window size list: a named size, or Custom, which keeps the size typed in.</summary>
public sealed class PlayerWindowSizeOption
{
    private PlayerWindowSizeOption(int width, int height, string name)
    {
        this.Width = width;
        this.Height = height;
        this.Name = name;
    }

    /// <summary>Gets the entry that keeps the width and height typed in.</summary>
    public static PlayerWindowSizeOption Custom { get; } = new(0, 0, "Custom");

    /// <summary>Gets the framebuffer width in physical pixels; 0 for Custom.</summary>
    public int Width { get; }

    /// <summary>Gets the framebuffer height in physical pixels; 0 for Custom.</summary>
    public int Height { get; }

    /// <summary>Gets the size's common name.</summary>
    public string Name { get; }

    /// <summary>Gets a value indicating whether this is the Custom entry.</summary>
    public bool IsCustom => ReferenceEquals(this, Custom);

    /// <summary>Gets the text the list shows, for example "1920 × 1080 (Full HD)" or "Custom…".</summary>
    public string Label => this.IsCustom
        ? "Custom…"
        : string.Create(CultureInfo.CurrentCulture, $"{this.Width} × {this.Height} ({this.Name})");

    /// <summary>Creates a named size.</summary>
    /// <param name="width">The framebuffer width in physical pixels.</param>
    /// <param name="height">The framebuffer height in physical pixels.</param>
    /// <param name="name">The size's common name.</param>
    /// <returns>The entry.</returns>
    public static PlayerWindowSizeOption Named(int width, int height, string name) => new(width, height, name);
}
