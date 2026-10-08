// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>Provides separate containers so toolbar overflow never changes an item's authored visibility.</summary>
/// <remarks>
/// A disabled control raises no pointer events, so its tooltip cannot open. The container shows a
/// disabled command's text tooltip instead, which keeps the reason a command is unavailable visible.
/// </remarks>
public sealed partial class ToolBarItemsControl : ItemsControl
{
    private static readonly ConditionalWeakTable<ContentPresenter, DisabledToolTip> DisabledToolTips = new();

    /// <inheritdoc />
    protected override bool IsItemItsOwnContainerOverride(object item) => false;

    /// <inheritdoc />
    protected override DependencyObject GetContainerForItemOverride() => new ContentPresenter();

    /// <inheritdoc />
    protected override void PrepareContainerForItemOverride(DependencyObject element, object item)
    {
        base.PrepareContainerForItemOverride(element, item);
        if (element is ContentPresenter container && item is Control command)
        {
            DisabledToolTips.AddOrUpdate(container, new DisabledToolTip(container, command));
        }
    }

    /// <inheritdoc />
    protected override void ClearContainerForItemOverride(DependencyObject element, object item)
    {
        if (element is ContentPresenter container && DisabledToolTips.TryGetValue(container, out var tooltip))
        {
            tooltip.Detach();
            _ = DisabledToolTips.Remove(container);
        }

        base.ClearContainerForItemOverride(element, item);
    }

    private sealed class DisabledToolTip
    {
        private readonly ContentPresenter container;
        private readonly Control command;
        private readonly long toolTipToken;

        public DisabledToolTip(ContentPresenter container, Control command)
        {
            this.container = container;
            this.command = command;
            command.IsEnabledChanged += this.OnIsEnabledChanged;
            this.toolTipToken = command.RegisterPropertyChangedCallback(ToolTipService.ToolTipProperty, this.OnToolTipChanged);
            this.Update();
        }

        public void Detach()
        {
            this.command.IsEnabledChanged -= this.OnIsEnabledChanged;
            this.command.UnregisterPropertyChangedCallback(ToolTipService.ToolTipProperty, this.toolTipToken);
            ToolTipService.SetToolTip(this.container, value: null);
        }

        private void OnIsEnabledChanged(object sender, DependencyPropertyChangedEventArgs args) => this.Update();

        private void OnToolTipChanged(DependencyObject sender, DependencyProperty property) => this.Update();

        private void Update()
            => ToolTipService.SetToolTip(
                this.container,
                !this.command.IsEnabled && ToolTipService.GetToolTip(this.command) is string { Length: > 0 } text ? text : null);
    }
}
