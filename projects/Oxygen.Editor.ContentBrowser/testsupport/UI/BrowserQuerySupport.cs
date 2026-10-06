// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser.TestSupport;

internal static class BrowserQuerySupport
{
    internal static void SetFilterChoice(FrameworkElement flyout, string label)
    {
        var choice = flyout.FindDescendant<CheckBox>(box => string.Equals(box.Content as string, label, StringComparison.Ordinal))!;
        _ = choice.Should().NotBeNull();
        ((IToggleProvider)new CheckBoxAutomationPeer(choice).GetPattern(PatternInterface.Toggle)).Toggle();
    }
}
