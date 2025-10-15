// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus.Tests;

/// <summary>
///     A testable subclass of <see cref="MenuItem" /> that exposes protected members for testing.
/// </summary>
public sealed partial class TestableMenuItem : MenuItem
{
    /*
     * Note that if you want to have this derived control class to also get the
     * same style and control template than the base class, you need to add the
     * base control Generic.xaml to the App.xaml ResourceDictionary, and you
     * need to add a "forwarding" style for this derived class too, like this:
     * <Style BasedOn="{StaticResource DefaultMenuItemStyle}"
     *      TargetType="local:TestableMenuItem" />
     */

    public void InvokeTapped()
    {
        var args = new TappedRoutedEventArgs();
        this.TryInvoke(MenuInteractionInputSource.PointerInput);
    }
}
