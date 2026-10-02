// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.MaterialEditor.Tests;

[TestClass]
public sealed class MaterialTextureChannelTests
{
    [TestMethod]
    public void MissingAssignmentIsVisibleAndCanBeReplacedOrCleared()
    {
        var changes = new List<string?>();
        var channel = new MaterialTextureChannel("base_color", "Base color", (_, path) => changes.Add(path));
        var available = new MaterialTextureChoice("/Content/Textures/Available.otex", "Available", true);
        const string missing = "/Content/Textures/Missing.otex";

        channel.Refresh([available], missing);

        _ = channel.WarningText.Should().Contain("Base color");
        _ = channel.WarningText.Should().Contain(missing);
        _ = channel.Choices.Should().Contain(choice => choice.VirtualPath == missing && !choice.IsAvailable);
        var choices = channel.Choices;

        channel.Refresh([available], missing);

        _ = channel.Choices.Should().BeSameAs(choices);

        channel.SelectedVirtualPath = available.VirtualPath;
        _ = channel.WarningText.Should().BeEmpty();
        channel.SelectedVirtualPath = null;
        _ = channel.WarningText.Should().BeEmpty();
        _ = changes.Should().Equal(available.VirtualPath, null);
    }
}
