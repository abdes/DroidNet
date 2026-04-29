// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("MenuButtonTests")]
[TestCategory("UITest")]
public sealed class MenuButtonTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task PointerOpenedMenu_DoesNotMoveFocusToPopupRoot_Async() => EnqueueAsync(async () =>
    {
        var menuSource = new MenuBuilder()
            .AddMenuItem("Field of View")
            .Build();
        var textBox = new TextBox { Text = "focus owner" };
        var menuButton = new MenuButton
        {
            Content = "Camera",
            MenuSource = menuSource,
        };
        var panel = new StackPanel();
        panel.Children.Add(textBox);
        panel.Children.Add(menuButton);

        await LoadTestContentAsync(panel).ConfigureAwait(true);
        _ = textBox.Focus(Microsoft.UI.Xaml.FocusState.Programmatic);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = menuButton.Show(MenuNavigationMode.PointerInput);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = FocusManager.GetFocusedElement(menuButton.XamlRoot)
            .Should()
            .BeSameAs(textBox, "pointer-opened menus leave focus alone so embedded editors can keep their TextBox focus");
    });
}
