// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector.Controls;
using Oxygen.Editor.World.Inspector.Geometry;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class AssetPickerContentTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task PickerContentInvokeReportsTheOriginalTypedRow(bool material) => EnqueueAsync(async () =>
    {
        var asset = new AssetPickerRow(new("Cube", new("asset:///Engine/Geometry/Cube.ogeo"), "Static Mesh", "/Engine/Geometry/Cube.ogeo", AssetPickerGroup.Engine, IsEnabled: true, "\uF158"));
        var none = new MaterialPickerRow(new("None", Uri: null, "No material", "No explicit override", AssetPickerGroup.Engine, IsEnabled: true, "\uE790"));
        var view = new AssetPickerContent
        {
            Width = 440,
            IsMaterial = material,
            Groups = material ? new[] { new MaterialGroup("Assignment", [none]) } : (object)new[] { new AssetGroup("Engine", [asset]) },
        };
        AssetPickerItemInvokedEventArgs? invocation = null;
        object? eventSender = null;
        view.ItemClicked += (sender, args) =>
        {
            eventSender = sender;
            invocation = args;
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var row = material ? (object)none : asset;
        var button = view.FindDescendant<Button>(candidate => ReferenceEquals(candidate.DataContext, row))!;
        _ = button.Should().NotBeNull();
        ((IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke)).Invoke();
        _ = eventSender.Should().BeSameAs(view);
        _ = invocation.Should().NotBeNull();
        if (material)
        {
            _ = invocation!.Material.Should().BeSameAs(none);
            _ = invocation.Asset.Should().BeNull();
        }
        else
        {
            _ = invocation!.Asset.Should().BeSameAs(asset);
            _ = invocation.Material.Should().BeNull();
        }
    });
}
