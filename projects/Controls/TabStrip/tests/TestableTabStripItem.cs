// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
///     A testable subclass of <see cref="TabStripItem" /> that exposes protected members for testing.
/// </summary>
public sealed partial class TestableTabStripItem : TabStripItem
{
    public new void OnPinClicked() => base.OnPinClicked();

    public new void OnCloseClicked() => base.OnCloseClicked();

    public new void OnPointerEntered() => base.OnPointerEntered();

    public new void OnPointerExited() => base.OnPointerExited();
}
