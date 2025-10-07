// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Runtime.InteropServices;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Describes the context in which a menu interaction is happening.
/// </summary>
/// <remarks>
///     The context <see cref="Kind"/> determines whether the interaction is happening within the root surface
///     (e.g. a menu bar) or within a specific cascading column surface. The surface property of the context for the specified
///     kind must be set to a valid <see cref="IMenuInteractionSurface"/> object. Note however, that either kind may optionally
///     also have a reference to the other surface kind, if applicable. For example, a root context may also have a reference
///     to the column surface currently rendering flyout columns, if any. Similarly, a column context that is attached to a root
///     surface may also specify that root surface, if known.
/// </remarks>
[StructLayout(LayoutKind.Auto)]
public readonly struct MenuInteractionContext : IEquatable<MenuInteractionContext>
{
    private MenuInteractionContext(MenuInteractionContextKind kind, MenuLevel columnLevel, IRootMenuSurface? rootSurface, ICascadedMenuSurface? columnSurface)
    {
        this.Kind = kind;
        this.ColumnLevel = columnLevel;
        this.RootSurface = rootSurface;
        this.ColumnSurface = columnSurface;
    }

    /// <summary>
    ///     Gets the interaction context kind. This is the only reliable and deterministic way to check
    ///     if an interaction should be interpreted at the root level, or at the column level.
    /// </summary>
    public MenuInteractionContextKind Kind { get; }

    /// <summary>
    /// Gets a value indicating whether the context is for the root interaction surface.
    /// </summary>
    public bool IsRoot => this.Kind == MenuInteractionContextKind.Root;

    /// <summary>
    /// Gets a value indicating whether the context is for a column interaction surface.
    /// </summary>
    public bool IsColumn => this.Kind == MenuInteractionContextKind.Column;

    /// <summary>
    ///     Gets the zero-based column level associated with the context.
    /// </summary>
    public MenuLevel ColumnLevel { get; }

    /// <summary>
    ///     Gets the root interaction surface associated with the context, when available.
    /// </summary>
    public IRootMenuSurface? RootSurface { get; }

    /// <summary>
    ///     Gets the column interaction surface associated with the context, when available.
    /// </summary>
    public ICascadedMenuSurface? ColumnSurface { get; }

    /// <summary>
    ///     Determines whether two contexts are equal.
    /// </summary>
    /// <param name="left">The left operand.</param>
    /// <param name="right">The right operand.</param>
    /// <returns><see langword="true"/> when the contexts are equal; otherwise <see langword="false"/>.</returns>
    public static bool operator ==(MenuInteractionContext left, MenuInteractionContext right) => left.Equals(right);

    /// <summary>
    ///     Determines whether two contexts are not equal.
    /// </summary>
    /// <param name="left">The left operand.</param>
    /// <param name="right">The right operand.</param>
    /// <returns><see langword="true"/> when the contexts differ; otherwise <see langword="false"/>.</returns>
    public static bool operator !=(MenuInteractionContext left, MenuInteractionContext right) => !left.Equals(right);

    /// <summary>
    ///     Creates a root interaction context.
    /// </summary>
    /// <param name="rootSurface">The root surface coordinating menu-bar interactions.</param>
    /// <param name="columnSurface">Optional column surface currently rendering flyout columns.</param>
    /// <returns>A root context instance.</returns>
    public static MenuInteractionContext ForRoot(IRootMenuSurface rootSurface, ICascadedMenuSurface? columnSurface = null)
        => new(MenuInteractionContextKind.Root, MenuLevel.First, rootSurface, columnSurface);

    /// <summary>
    ///     Creates an interaction context for a specific column level.
    /// </summary>
    /// <param name="columnLevel">The zero-based column level (0 == root column).</param>
    /// <param name="columnSurface">The column surface coordinating cascading menu interactions.</param>
    /// <param name="rootSurface">Optional root surface associated with the column.</param>
    /// <returns>A column context instance.</returns>
    public static MenuInteractionContext ForColumn(MenuLevel columnLevel, ICascadedMenuSurface columnSurface, IRootMenuSurface? rootSurface = null)
        => new(MenuInteractionContextKind.Column, columnLevel, rootSurface, columnSurface);

    /// <inheritdoc />
    public bool Equals(MenuInteractionContext other) => this.Kind == other.Kind && this.ColumnLevel == other.ColumnLevel;

    /// <inheritdoc />
    public override bool Equals(object? obj) => obj is MenuInteractionContext other && this.Equals(other);

    /// <inheritdoc />
    public override int GetHashCode() => HashCode.Combine((int)this.Kind, this.ColumnLevel);

    /// <summary>
    ///     Ensures the validity of the interaction context.
    /// </summary>
    [Conditional("DEBUG")]
    internal void EnsureValid()
    {
        Debug.Assert(this.Kind != MenuInteractionContextKind.Root || this.RootSurface is not null, "Root context must have a valid root surface");
        Debug.Assert(this.ColumnSurface is null || this.ColumnLevel >= 0, "If a column surface is specified, the column level must be positive");
        Debug.Assert(this.Kind != MenuInteractionContextKind.Column || this.ColumnSurface is not null, "Column context must have a valid column surface");
    }
}
