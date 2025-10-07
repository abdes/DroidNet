// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;

namespace DroidNet.Controls.Menus;

/// <summary>
///     AutomationPeer for <see cref="MenuItem"/>. Exposes the Invoke pattern and accelerator information so
///     accessibility tools and access-key handlers can activate menu items programmatically.
/// </summary>
/// <remarks>
///     Initializes a new instance of the <see cref="MenuItemAutomationPeer"/> class.
/// </remarks>
/// <param name="owner">The associated <see cref="MenuItem"/>.</param>
public sealed partial class MenuItemAutomationPeer(MenuItem owner) : FrameworkElementAutomationPeer(owner), IInvokeProvider
{
    private readonly MenuItem owner = owner ?? throw new ArgumentNullException(nameof(owner));

    /// <summary>
    ///     Invokes the MenuItem. This is the IInvokeProvider implementation.
    /// </summary>
    public void Invoke() => _ = this.owner.DispatcherQueue.TryEnqueue(() => _ = this.owner.TryExpandOrInvoke(MenuInteractionInputSource.Programmatic));

    /// <inheritdoc/>
    protected override string GetClassNameCore() => nameof(MenuItem);

    /// <inheritdoc/>
    protected override AutomationControlType GetAutomationControlTypeCore()
        => AutomationControlType.MenuItem;

    /// <inheritdoc/>
    protected override bool IsControlElementCore() => true;

    /// <inheritdoc/>
    protected override bool IsContentElementCore() => true;

    /// <inheritdoc/>
    protected override string GetNameCore()
    {
        var name = base.GetNameCore();
        return !string.IsNullOrEmpty(name)
            ? name
            : this.owner.ItemData?.Text ?? string.Empty;
    }

    /// <inheritdoc/>
    protected override string GetAcceleratorKeyCore()
        => this.owner.ItemData?.AcceleratorText ?? string.Empty;

    /// <inheritdoc/>
    protected override string GetAccessKeyCore()
    {
        var m = this.owner.ItemData?.Mnemonic;
        return m.HasValue ? m.Value.ToString() : string.Empty;
    }

    /// <inheritdoc/>
    protected override object? GetPatternCore(PatternInterface patternInterface)
        => patternInterface == PatternInterface.Invoke
            ? this
            : base.GetPatternCore(patternInterface);
}
