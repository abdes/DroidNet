// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents a host surface capable of responding to interaction commands issued by
///     <see cref="MenuInteractionController"/>. This indirection allows unit tests to mock menu
///     surfaces while production controls forward the calls to their existing helpers.
/// </summary>
public interface IMenuInteractionSurface
{
    /// <summary>
    ///     Dismisses the surface, closing any transient UI (for example, popups or panels) that the surface
    ///     manages. Implementations should ensure the surface is left in a stable, non-interactive state.
    /// </summary>
    /// <param name="kind">Indicates the reason or modality of the dismissal.</param>
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic);
}

/// <summary>
///     Represents a root (top-level) menu interaction surface.
/// </summary>
/// <remarks>
///     A root surface models a flat, single-depth collection of actionable items (e.g., a menu bar or toolbar), where
///     interaction does not create additional nested panes. Items are presented in one plane, and opening an item
///     either executes an action or transiently reveals a new menu interaction surface without establishing persistent
///     cascading state.
///     <para>
///     Conceptually, a root surface is a single-row (or column) menu interaction surface, which can be presented
///     horizontally (or vertically).
///     </para>
/// </remarks>
public interface IRootMenuSurface : IMenuInteractionSurface
{
    /// <summary>
    ///     Gets the UI element that owns or represents this root surface for focus management purposes.
    /// </summary>
    /// <remarks>
    ///     This property provides access to the underlying UI element that should be used when capturing
    ///     or restoring focus during menu interactions. For menu bars, this is typically the menu bar control
    ///     itself. For context menus, this is the trigger element that invoked the menu.
    /// </remarks>
    /// <returns>
    ///     A <see cref="object"/> representing the focus owner element, typically a <see cref="Microsoft.UI.Xaml.UIElement"/>;
    ///     or <see langword="null"/> if no focus owner is available.
    /// </returns>
    public object? FocusElement { get; }

    /// <summary>
    ///     Materializes the initial root menu UI for this surface.
    /// </summary>
    /// <param name="navigationMode">
    ///     The input modality that triggered the menu request (keyboard, pointer, or programmatic).
    /// </param>
    /// <remarks>
    ///     Implementations should render or present the initial menu for interaction.
    ///     For a menu bar this may be a no-op or set initial focus. For context/popup menus,
    ///     this should display the flyout at the previously captured trigger location.
    /// </remarks>
    public void Show(MenuNavigationMode navigationMode);

    /// <summary>
    ///     Retrieves the <em>neighboring item</em> in the root sequence relative to <paramref name="itemData"/>,
    ///     according to the navigation <paramref name="direction"/> on the root axis.
    /// </summary>
    /// <param name="itemData">
    ///     The anchor item in the root sequence. The surface resolves this item by identity and determines its adjacent
    ///     neighbor on the root axis (e.g., next to the right/down or previous to the left/up).
    /// </param>
    /// <param name="direction">
    ///     The root-axis movement (e.g., right/down or left/up) used to select the adjacent item.
    /// </param>
    /// <param name="wrap">
    ///     <see langword="true"/> to apply cyclic navigation on the root axis, promoting the first item when moving
    ///     forward from the last, or the last item when moving backward from the first; <see langword="false"/> to
    ///     clamp at boundaries, returning <paramref name="itemData"/> itself when the movement would exceed the range.
    ///     The default is <see langword="true"/>.
    /// </param>
    /// <returns>
    ///     The adjacent <see cref="MenuItemData"/> on the root axis, or <paramref name="itemData"/> when clamped at a
    ///     boundary with <paramref name="wrap"/>=<see langword="true"/>.
    /// </returns>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of this root surface’s item sequence.
    /// </exception>
    public MenuItemData GetAdjacentItem(
        MenuItemData itemData,
        MenuNavigationDirection direction,
        bool wrap = true);

    /// <summary>
    ///     Gets the item that is currently <em>expanded</em> in the root surface (i.e., the item whose activation has
    ///     transiently revealed a subordinate interaction surface or popup), if any.
    /// </summary>
    /// <returns>
    ///     The <see cref="MenuItemData"/> that is currently <em>expanded</em>; or <see langword="null"/> if no root
    ///     item is expanded.
    /// </returns>
    public MenuItemData? GetExpandedItem();

    /// <summary>
    ///     Gets the item that currently owns the <em>focus</em> within the root surface (the item that receives
    ///     keyboard input).
    /// </summary>
    /// <returns>
    ///     The <see cref="MenuItemData"/> that is the current focus anchor, or <see langword="null"/> if no item is
    ///     focused.
    /// </returns>
    public MenuItemData? GetFocusedItem();

    /// <summary>
    ///     Attempts to move focus to the specified root item and establish it as the current <em>focus anchor</em>
    ///     for subsequent menu keyboard navigation. Focus transfer should not result in expansion of the item.
    /// </summary>
    /// <param name="itemData">The item to become the focus anchor. The item MUST be focusable.</param>
    /// <param name="navigationMode">
    ///     The input modality used to perform the focus transfer (e.g., keyboard, pointer, or programmatic), which may
    ///     influence visual cues and announcement semantics.
    /// </param>
    /// <returns>
    ///     <see langword="true"/> if focus was successfully set on <paramref name="itemData"/>; otherwise
    ///     <see langword="false"/> (for example, when the item is not focusable).
    /// </returns>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of this root surface’s items.
    /// </exception>
    public bool FocusItem(MenuItemData itemData, MenuNavigationMode navigationMode);

    /// <summary>
    ///     Moves focus to the first <em>focusable</em> item in the root sequence, establishing the canonical start
    ///     position used for root-axis keyboard traversal.
    /// </summary>
    /// <param name="navigationMode">
    ///     The input modality that caused the focus transfer (for example, keyboard, pointer, or programmatic), which
    ///     implementers can use to adjust visual focus cues and announcement behavior.
    /// </param>
    /// <returns>
    ///     <see langword="true"/> if focus was successfully set on the first focusable item; otherwise
    ///     <see langword="false"/> if no focusable item could be found or focused.
    /// </returns>
    public bool FocusFirstItem(MenuNavigationMode navigationMode);

    /// <summary>
    ///     Expands the specified root item, materializing its subordinate interaction surface (for example, a dropdown
    ///     panel or popup) while preserving the root surface’s single-depth semantics.
    /// </summary>
    /// <param name="itemData">
    ///     The item to expand. Expansion presents a subordinate surface anchored to this root item.
    /// </param>
    /// <param name="navigationMode">
    ///     The input modality that triggered expansion (for example, keyboard, pointer, or programmatic), which may
    ///     influence focus transfer into the subordinate surface and announcement behavior.
    /// </param>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of this root surface’s item sequence.
    /// </exception>
    public void ExpandItem(MenuItemData itemData, MenuNavigationMode navigationMode);

    /// <summary>
    ///     Collapses the specified root item, dismissing any subordinate surface that the item materialized while
    ///     keeping the root surface active.
    /// </summary>
    /// <param name="itemData">The item to collapse. The item must be part of this root surface's sequence.</param>
    /// <param name="navigationMode">
    ///     The input modality that triggered the collapse (for example, keyboard, pointer, or programmatic).
    /// </param>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of this root surface’s items.
    /// </exception>
    public void CollapseItem(MenuItemData itemData, MenuNavigationMode navigationMode);
}

/// <summary>
///     Represents a cascaded menu interaction surface.
/// </summary>
/// <remarks>
///     A cascaded menu surface represents a dynamically evolving stack of columns where each column is a slice of the
///     navigation context. Expanding an item in column `N` may materialize a child column at `N+1`. This surface
///     supports arbitrarily deep nesting and deterministic trimming of columns when the interaction context changes.
///     <para>
///     Conceptually, a cascaded menu surface:
///     - Maintains an ordered, zero-based stack of columns where column 0 is the entry column for this surface.
///     - Encodes hierarchical navigation: selecting an item with children pushes (or refreshes) the next column.
///     - Guarantees logical boundaries: only one linear chain of open columns exists; lateral branches are realized
///       by replacing tail columns rather than forking.
///     - Provides deterministic “trim” semantics to collapse trailing context when the user navigates back or selects
///       a terminal item, ensuring UI consistency and avoiding stale columns.
///     - Keyboard semantics typically scope to the active column, promoting or collapsing columns as interactions
///       change the hierarchy, but may also extend to adjacent columns or spill over to a potential owner root surface.
///     </para>
/// </remarks>
public interface ICascadedMenuSurface : IMenuInteractionSurface
{
    /// <summary>
    ///     Retrieves the neighboring item within the specified column <paramref name="level"/> relative to
    ///     <paramref name="itemData"/>, according to the provided <paramref name="direction"/>.
    /// </summary>
    /// <param name="level">The zero-based column level containing the anchor item.</param>
    /// <param name="itemData">The anchor item in the column whose neighbor is to be resolved.</param>
    /// <param name="direction">The root-axis movement (for example, right/down or left/up) used to select the adjacent item.</param>
    /// <param name="wrap">
    ///     <see langword="true"/> to enable cyclic navigation within the column (promote first when moving forward
    ///     from the last, or promote last when moving backward from the first); <see langword="false"/> to clamp at
    ///     boundaries, returning <paramref name="itemData"/> when movement would exceed the range. The default is
    ///     <see langword="true"/>.
    /// </param>
    /// <returns>The adjacent <see cref="MenuItemData"/> within the column.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    /// <exception cref="ArgumentException">Thrown when <paramref name="itemData"/> is not part of the specified column.</exception>
    public MenuItemData GetAdjacentItem(MenuLevel level, MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true);

    /// <summary>
    ///     Gets the item that is currently expanded within the specified column, if any.
    /// </summary>
    /// <param name="level">The zero-based column level to inspect.</param>
    /// <returns>
    ///     The <see cref="MenuItemData"/> that is currently expanded in the column; or <see langword="null"/> if no
    ///     item is expanded at that level.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    public MenuItemData? GetExpandedItem(MenuLevel level);

    /// <summary>
    ///     Gets the item that currently owns focus within the specified column (the item that receives keyboard
    ///     input for that column), if any.
    /// </summary>
    /// <param name="level">The zero-based column level to inspect.</param>
    /// <returns>
    ///     The <see cref="MenuItemData"/> that is the current focus anchor at the given level; or <see langword="null"/>
    ///     if no item is focused.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    public MenuItemData? GetFocusedItem(MenuLevel level);

    /// <summary>
    ///     Attempts to move focus to the specified item within the given column <paramref name="level"/> and
    ///     establish it as the focus anchor for that column. The target <paramref name="itemData"/> MUST be focusable.
    /// </summary>
    /// <param name="level">The zero-based column level containing the item.</param>
    /// <param name="itemData">The item to receive focus; must be focusable.</param>
    /// <param name="navigationMode">
    ///     The input modality used to perform the focus transfer (for example, keyboard, pointer, or programmatic).
    /// </param>
    /// <returns>
    ///     <see langword="true"/> if focus was successfully set on <paramref name="itemData"/>; otherwise
    ///     <see langword="false"/> (for example, when the item is not focusable).
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    public bool FocusItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode);

    /// <summary>
    ///     Moves focus to the first <em>focusable</em> item in the specified column <paramref name="level"/>,
    ///     establishing the canonical start position for keyboard traversal within that column.
    /// </summary>
    /// <param name="level">The zero-based column level to operate on.</param>
    /// <param name="navigationMode">
    ///     The input modality that caused the focus transfer (for example, keyboard, pointer, or programmatic), which
    ///     implementers can use to adjust visual focus cues and announcement behavior.
    /// </param>
    /// <returns>
    ///     <see langword="true"/> if focus was successfully set on the first focusable item; otherwise
    ///     <see langword="false"/> if no focusable item could be found or focused.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    public bool FocusFirstItem(MenuLevel level, MenuNavigationMode navigationMode);

    /// <summary>
    ///     Expands the specified item in the given column level, materializing its subordinate interaction surface
    ///     (for example, a child column or popup) and updating the column stack accordingly.
    /// </summary>
    /// <param name="level">The zero-based column level containing the item to expand.</param>
    /// <param name="itemData">The item to expand; expansion presents its subordinate surface anchored to this item.</param>
    /// <param name="navigationMode">
    ///     The input modality used to trigger expansion (for example, keyboard, pointer, or programmatic), which may
    ///     influence focus transfer and announcement behavior.
    /// </param>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of the specified column’s item sequence.
    /// </exception>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    public void ExpandItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode);

    /// <summary>
    ///     Collapses the specified item in the given column level, dismissing its subordinate surface and trimming the
    ///     column stack as required to maintain a valid cascade.
    /// </summary>
    /// <param name="level">The zero-based column level containing the item to collapse.</param>
    /// <param name="itemData">The item to collapse; it must belong to the specified column.</param>
    /// <param name="navigationMode">
    ///     The input modality that triggered the collapse (for example, keyboard, pointer, or programmatic).
    /// </param>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of the specified column’s item sequence.
    /// </exception>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> does not correspond to an open column on this surface.
    /// </exception>
    public void CollapseItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode);

    /// <summary>
    ///     Keeps columns up to the specified level open, and closes all columns after.
    /// </summary>
    /// <param name="level">The zero-based column level to trim to.</param>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="level"/> is negative or exceeds the number of open columns on this surface.
    /// </exception>
    public void TrimTo(MenuLevel level);
}
