// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class SceneNodeFlagHelpersTests
{
    private readonly JsonSerializerOptions jsonOptions = new();
    private readonly Mock<IProject> projectMock = new();

    public SceneNodeFlagHelpersTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new SceneJsonConverter(this.ExampleProject));
    }

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void GetFlags_And_SetFlags_Roundtrip()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "HScene" };
        using var node = new SceneNode(scene) { Name = "HNode" };

        // Act
        node.IsVisible = false;
        node.CastsShadows = true;
        node.IsRayCastingSelectable = false;
        node.IsStatic = true;

        var flags = node.Flags;

        // Create a fresh node and apply flags
        using var node2 = new SceneNode(scene) { Name = "HNode2" };
        node2.Flags = flags;

        // Assert
        _ = node2.IsVisible.Should().BeFalse();
        _ = node2.CastsShadows.Should().BeTrue();
        _ = node2.IsRayCastingSelectable.Should().BeFalse();
        _ = node2.IsStatic.Should().BeTrue();
    }

    [TestMethod]
    public void ToggleFlag_Works_And_HasFlag()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "TScene" };
        using var node = new SceneNode(scene) { Name = "TNode" };

        // Initially defaults: Visible=true, CastsShadows=false
        _ = node.HasFlag(SceneNodeFlags.Visible).Should().BeTrue();
        _ = node.HasFlag(SceneNodeFlags.CastsShadows).Should().BeFalse();

        // Toggle CastsShadows on
        node.ToggleFlag(SceneNodeFlags.CastsShadows);
        _ = node.HasFlag(SceneNodeFlags.CastsShadows).Should().BeTrue();

        // Toggle Visible off
        node.ToggleFlag(SceneNodeFlags.Visible);
        _ = node.HasFlag(SceneNodeFlags.Visible).Should().BeFalse();
    }

    [TestMethod]
    public void Bitmask_Conversions_Work()
    {
        // Arrange
        var f = SceneNodeFlags.Visible | SceneNodeFlags.Static;

        // Act
        var mask = f.ToBitmask();
        var round = SceneNodeFlagsExtensions.FromBitmask(mask);

        // Assert
        _ = round.HasFlag(SceneNodeFlags.Visible).Should().BeTrue();
        _ = round.HasFlag(SceneNodeFlags.Static).Should().BeTrue();
    }

    [TestMethod]
    public void Helper_RoundTrips_Through_Json_Converter()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "JsonScene" };
        using var node = new SceneNode(scene) { Name = "JsonNode" };
        node.IsVisible = false;
        node.CastsShadows = true;

        scene.RootNodes.Add(node);

        // Act
        var json = Scene.ToJson(scene);
        var des = Scene.FromJson(json, this.ExampleProject);

        // Assert
        _ = des.Should().NotBeNull();
        Debug.Assert(des is not null, "des != null");
        var rnode = des!.RootNodes.First();
        _ = rnode.IsVisible.Should().BeFalse();
        _ = rnode.CastsShadows.Should().BeTrue();
    }
}
