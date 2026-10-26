// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;

namespace DroidNet.Controls;

/// <summary>
///     Automation peer for <see cref="DynamicTreeItem" />. Presents the row as a tree item with its label as name,
///     its depth as level, and the ExpandCollapse and SelectionItem patterns.
/// </summary>
/// <remarks>
///     The peer only requests changes: expansion goes through the row's <see cref="DynamicTreeItem.Expand" /> and
///     <see cref="DynamicTreeItem.Collapse" /> events and selection through the owning tree, so the view model stays
///     the single decision point.
/// </remarks>
/// <param name="owner">The <see cref="DynamicTreeItem" /> this peer represents.</param>
public sealed partial class DynamicTreeItemAutomationPeer(DynamicTreeItem owner)
    : FrameworkElementAutomationPeer(owner), IExpandCollapseProvider, ISelectionItemProvider
{
    private readonly DynamicTreeItem owner = owner;

    /// <inheritdoc />
    public ExpandCollapseState ExpandCollapseState
        => this.owner.ItemAdapter switch
        {
            null or { CanAcceptChildren: false, ChildrenCount: 0 } => ExpandCollapseState.LeafNode,
            { IsExpanded: true } => ExpandCollapseState.Expanded,
            _ => ExpandCollapseState.Collapsed,
        };

    /// <inheritdoc />
    public bool IsSelected => this.owner.ItemAdapter?.IsSelected == true;

    /// <inheritdoc />
    public IRawElementProviderSimple SelectionContainer
        => this.owner.OwnerTree is { } tree && FromElement(tree) is { } peer ? this.ProviderFromPeer(peer) : null!; // a row outside a tree has no container

    /// <inheritdoc />
    public void Expand() => this.owner.RequestExpansion(expand: true);

    /// <inheritdoc />
    public void Collapse() => this.owner.RequestExpansion(expand: false);

    /// <inheritdoc />
    public void Select() => this.owner.RequestSelection(toggle: false);

    /// <inheritdoc />
    public void AddToSelection()
    {
        if (!this.IsSelected)
        {
            this.owner.RequestSelection(toggle: true);
        }
    }

    /// <inheritdoc />
    public void RemoveFromSelection()
    {
        if (this.IsSelected)
        {
            this.owner.RequestSelection(toggle: true);
        }
    }

    /// <summary>Notifies assistive technologies that the expanded state of the row changed.</summary>
    /// <param name="wasExpanded">The previous expanded state.</param>
    /// <param name="isExpanded">The new expanded state.</param>
    internal void RaiseExpandCollapseStateChanged(bool wasExpanded, bool isExpanded)
        => this.RaisePropertyChangedEvent(
            ExpandCollapsePatternIdentifiers.ExpandCollapseStateProperty,
            wasExpanded ? ExpandCollapseState.Expanded : ExpandCollapseState.Collapsed,
            isExpanded ? ExpandCollapseState.Expanded : ExpandCollapseState.Collapsed);

    /// <summary>Notifies assistive technologies that the selected state of the row changed.</summary>
    /// <param name="isSelected">The new selected state.</param>
    internal void RaiseIsSelectedChanged(bool isSelected)
        => this.RaisePropertyChangedEvent(SelectionItemPatternIdentifiers.IsSelectedProperty, !isSelected, isSelected);

    /// <inheritdoc />
    protected override string GetClassNameCore() => nameof(DynamicTreeItem);

    /// <inheritdoc />
    protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.TreeItem;

    /// <inheritdoc />
    protected override string GetNameCore()
        => this.owner.ItemAdapter?.Label is { Length: > 0 } label ? label : base.GetNameCore();

    /// <inheritdoc />
    protected override int GetLevelCore() => this.owner.ItemAdapter is { } item ? Math.Max(0, item.Depth) + 1 : base.GetLevelCore();

    /// <inheritdoc />
    protected override object? GetPatternCore(PatternInterface patternInterface)
        => patternInterface switch
        {
            PatternInterface.ExpandCollapse or PatternInterface.SelectionItem => this,
            _ => base.GetPatternCore(patternInterface),
        };
}
