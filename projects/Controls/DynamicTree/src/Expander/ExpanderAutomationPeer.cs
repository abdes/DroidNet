// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;

namespace DroidNet.Controls;

/// <summary>
///     Automation peer for the <see cref="Expander" /> control. Exposes the ExpandCollapse pattern so
///     assistive technologies can read and request the expanded or collapsed state.
/// </summary>
/// <remarks>
///     Like the control itself, the peer only requests the change by raising the
///     <see cref="Expander.Expand" /> or <see cref="Expander.Collapse" /> event; the handlers decide
///     whether to honor the request and update <see cref="Expander.IsExpanded" />.
/// </remarks>
/// <param name="owner">The <see cref="Expander" /> this peer represents.</param>
public sealed partial class ExpanderAutomationPeer(Expander owner) : FrameworkElementAutomationPeer(owner), IExpandCollapseProvider
{
    private readonly Expander owner = owner;

    /// <inheritdoc />
    public ExpandCollapseState ExpandCollapseState
        => this.owner.IsExpanded ? ExpandCollapseState.Expanded : ExpandCollapseState.Collapsed;

    /// <inheritdoc />
    public void Expand()
    {
        if (!this.owner.IsExpanded)
        {
            this.owner.Toggle();
        }
    }

    /// <inheritdoc />
    public void Collapse()
    {
        if (this.owner.IsExpanded)
        {
            this.owner.Toggle();
        }
    }

    /// <summary>
    ///     Notifies assistive technologies that the expanded state of the owner changed.
    /// </summary>
    /// <param name="wasExpanded">The previous value of <see cref="Expander.IsExpanded" />.</param>
    /// <param name="isExpanded">The new value of <see cref="Expander.IsExpanded" />.</param>
    internal void RaiseExpandCollapseStateChanged(bool wasExpanded, bool isExpanded)
        => this.RaisePropertyChangedEvent(
            ExpandCollapsePatternIdentifiers.ExpandCollapseStateProperty,
            wasExpanded ? ExpandCollapseState.Expanded : ExpandCollapseState.Collapsed,
            isExpanded ? ExpandCollapseState.Expanded : ExpandCollapseState.Collapsed);

    /// <inheritdoc />
    protected override string GetClassNameCore() => nameof(Expander);

    /// <inheritdoc />
    protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Button;

    /// <inheritdoc />
    protected override string GetNameCore()
    {
        var name = base.GetNameCore();
        return string.IsNullOrEmpty(name) ? nameof(Expander) : name;
    }

    /// <inheritdoc />
    protected override object? GetPatternCore(PatternInterface patternInterface)
        => patternInterface == PatternInterface.ExpandCollapse
            ? this
            : base.GetPatternCore(patternInterface);
}
