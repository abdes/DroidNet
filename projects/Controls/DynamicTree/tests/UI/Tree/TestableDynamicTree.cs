// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Windows.System;

namespace DroidNet.Controls.Tests.Tree;

public sealed partial class TestableDynamicTree : DynamicTree
{
    public bool InvokeItemPointerPressed(TreeItemAdapter item, bool isControlDown, bool isShiftDown, bool leftButtonPressed)
        => this.OnItemPointerPressed(item, isControlDown, isShiftDown, leftButtonPressed);

    public bool InvokeItemTapped(TreeItemAdapter item, bool isControlDown, bool isShiftDown)
        => this.OnItemTapped(item, isControlDown, isShiftDown);

    public bool InvokeItemGotFocus(TreeItemAdapter item, bool isApplyingFocus)
        => this.OnItemGotFocus(item, isApplyingFocus);

    public void InvokeTreeGotFocus()
        => this.OnGotFocus(new RoutedEventArgs());

    public Task<bool> InvokeHandleKeyDownAsync(VirtualKey key, bool isControlDown = false, bool isShiftDown = false)
        => this.HandleKeyDownAsync(key, isControlDown, isShiftDown);
}
