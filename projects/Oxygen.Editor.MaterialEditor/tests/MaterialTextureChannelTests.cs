// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor.Tests;

[TestClass]
public sealed class MaterialTextureChannelTests
{
    [TestMethod]
    public void MissingAssignmentIsVisibleAndCanBeReplacedOrCleared()
    {
        var changes = new List<string?>();
        var channel = new MaterialTextureChannel("base_color", "Base color", (_, path) => changes.Add(path));
        var available = CreateTexture(AssetPath.Parse("/Content/Textures/Available.otex"));
        const string missing = "/Content/Textures/Missing.otex";

        channel.Refresh([available], missing);

        _ = channel.WarningVisibility.Should().Be(Microsoft.UI.Xaml.Visibility.Visible);
        _ = channel.SelectedText.Should().EndWith(missing);
        _ = channel.Choices.HasClear.Should().BeTrue();
        _ = channel.Choices.Find(available.Identity).Should().NotBeNull();

        channel.SelectedVirtualPath = available.Identity.VirtualPath;
        _ = channel.WarningText.Should().BeEmpty();
        _ = channel.SelectedText.Should().Be(available.DisplayName);
        channel.SelectedVirtualPath = null;
        _ = channel.WarningText.Should().BeEmpty();
        _ = changes.Should().Equal(available.Identity.VirtualPath, null);
    }

    private static AssetEntry CreateTexture(AssetPath asset) => new(
        asset, "Available", AssetKind.Texture, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Unknown, asset.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true);
}
