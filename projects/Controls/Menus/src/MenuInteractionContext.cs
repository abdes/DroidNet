// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Controls;

/// <summary>
///     Describes the context in which a menu interaction occurred.
/// </summary>
[StructLayout(LayoutKind.Auto)]
public readonly struct MenuInteractionContext : IEquatable<MenuInteractionContext>
{
    private MenuInteractionContext(MenuInteractionContextKind kind, int columnLevel, IMenuInteractionSurface? rootSurface, IMenuInteractionSurface? columnSurface)
    {
        this.Kind = kind;
        this.ColumnLevel = kind == MenuInteractionContextKind.Root ? 0 : columnLevel;
        this.RootSurface = rootSurface;
        this.ColumnSurface = columnSurface;
    }

    /// <summary>
    ///     Gets the interaction context kind.
    /// </summary>
    public MenuInteractionContextKind Kind { get; }

    /// <summary>
    ///     Gets the zero-based column level associated with the context.
    /// </summary>
    public int ColumnLevel { get; }

    /// <summary>
    ///     Gets the root interaction surface associated with the context, when available.
    /// </summary>
    public IMenuInteractionSurface? RootSurface { get; }

    /// <summary>
    ///     Gets the column interaction surface associated with the context, when available.
    /// </summary>
    public IMenuInteractionSurface? ColumnSurface { get; }

    /// <summary>
    ///     Gets the effective column level for interaction coordination.
    /// </summary>
    public int EffectiveColumnLevel => this.Kind == MenuInteractionContextKind.Root ? 0 : this.ColumnLevel;

    /// <summary>
    ///     Determines whether two contexts are equal.
    /// </summary>
    /// <param name="left">The left operand.</param>
    /// <param name="right">The right operand.</param>
    /// <returns><c>true</c> when the contexts are equal; otherwise <c>false</c>.</returns>
    public static bool operator ==(MenuInteractionContext left, MenuInteractionContext right) => left.Equals(right);

    /// <summary>
    ///     Determines whether two contexts are not equal.
    /// </summary>
    /// <param name="left">The left operand.</param>
    /// <param name="right">The right operand.</param>
    /// <returns><c>true</c> when the contexts differ; otherwise <c>false</c>.</returns>
    public static bool operator !=(MenuInteractionContext left, MenuInteractionContext right) => !left.Equals(right);

    /// <summary>
    ///     Creates a root interaction context.
    /// </summary>
    /// <param name="rootSurface">The root surface coordinating menu-bar interactions.</param>
    /// <param name="columnSurface">Optional column surface currently rendering flyout columns.</param>
    /// <returns>A root context instance.</returns>
    public static MenuInteractionContext ForRoot(IMenuInteractionSurface rootSurface, IMenuInteractionSurface? columnSurface = null)
    {
        ArgumentNullException.ThrowIfNull(rootSurface);
        return new MenuInteractionContext(MenuInteractionContextKind.Root, 0, rootSurface, columnSurface);
    }

    /// <summary>
    ///     Creates an interaction context for a specific column level.
    /// </summary>
    /// <param name="columnLevel">The zero-based column level (0 == root column).</param>
    /// <param name="columnSurface">The column surface coordinating cascading menu interactions.</param>
    /// <param name="rootSurface">Optional root surface associated with the column.</param>
    /// <returns>A column context instance.</returns>
    public static MenuInteractionContext ForColumn(int columnLevel, IMenuInteractionSurface columnSurface, IMenuInteractionSurface? rootSurface = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(columnLevel);
        ArgumentNullException.ThrowIfNull(columnSurface);
        return new MenuInteractionContext(MenuInteractionContextKind.Column, columnLevel, rootSurface, columnSurface);
    }

    /// <inheritdoc />
    public bool Equals(MenuInteractionContext other) => this.Kind == other.Kind && this.ColumnLevel == other.ColumnLevel;

    /// <inheritdoc />
    public override bool Equals(object? obj) => obj is MenuInteractionContext other && this.Equals(other);

    /// <inheritdoc />
    public override int GetHashCode() => HashCode.Combine((int)this.Kind, this.ColumnLevel);
}
