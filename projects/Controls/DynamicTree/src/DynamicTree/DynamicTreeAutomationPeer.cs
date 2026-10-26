// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;

namespace DroidNet.Controls;

/// <summary>
///     Automation peer for <see cref="DynamicTree" />. Presents the control as a tree whose realized rows are tree items,
///     and reports the selected realized rows through the Selection pattern.
/// </summary>
/// <param name="owner">The <see cref="DynamicTree" /> this peer represents.</param>
public sealed partial class DynamicTreeAutomationPeer(DynamicTree owner)
    : FrameworkElementAutomationPeer(owner), ISelectionProvider
{
    private readonly DynamicTree owner = owner;

    /// <inheritdoc />
    public bool CanSelectMultiple => this.owner.ViewModel?.SelectionMode == SelectionMode.Multiple;

    /// <inheritdoc />
    public bool IsSelectionRequired => false;

    /// <inheritdoc />
    public IRawElementProviderSimple[] GetSelection()
        => this.owner.GetRealizedRows()
            .Where(static row => row.ItemAdapter?.IsSelected == true)
            .Select(row => this.ProviderFromPeer(CreatePeerForElement(row)))
            .ToArray();

    /// <inheritdoc />
    protected override string GetClassNameCore() => nameof(DynamicTree);

    /// <inheritdoc />
    protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Tree;

    /// <inheritdoc />
    protected override object? GetPatternCore(PatternInterface patternInterface)
        => patternInterface == PatternInterface.Selection ? this : base.GetPatternCore(patternInterface);
}
