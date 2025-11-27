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
public class SceneNodeFlagsTests
{
    private readonly JsonSerializerOptions jsonOptions = new();
    private readonly Mock<IProject> projectMock = new();

    public SceneNodeFlagsTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new SceneJsonConverter(this.ExampleProject));
    }

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void SceneNode_Defaults_Are_As_Specified()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Test" };
        using var node = new SceneNode(scene) { Name = "Node" };

        // Act & Assert
        _ = node.IsVisible.Should().BeTrue();
        _ = node.IsRayCastingSelectable.Should().BeTrue();
        _ = node.CastsShadows.Should().BeFalse();
        _ = node.ReceivesShadows.Should().BeFalse();
        _ = node.IgnoreParentTransform.Should().BeFalse();
        _ = node.IsStatic.Should().BeFalse();
    }

    [TestMethod]
    public void SceneNode_Flags_Roundtrip_Serialization()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "FlagsScene" };
        using var node = new SceneNode(scene) { Name = "FlagsNode" };
        scene.RootNodes.Add(node);

        // Set non-defaults
        node.IsVisible = false;
        node.CastsShadows = true;
        node.ReceivesShadows = true;
        node.IsRayCastingSelectable = false;
        node.IgnoreParentTransform = true;
        node.IsStatic = true;

        // Act
        var json = Scene.ToJson(scene);
        var round = Scene.FromJson(json, this.ExampleProject);

        // Assert
        _ = round.Should().NotBeNull();
        Debug.Assert(round is not null, "round != null");
        _ = round!.RootNodes.Should().ContainSingle();
        var rnode = round.RootNodes.First();
        _ = rnode.IsVisible.Should().BeFalse();
        _ = rnode.CastsShadows.Should().BeTrue();
        _ = rnode.ReceivesShadows.Should().BeTrue();
        _ = rnode.IsRayCastingSelectable.Should().BeFalse();
        _ = rnode.IgnoreParentTransform.Should().BeTrue();
        _ = rnode.IsStatic.Should().BeTrue();
    }

    [TestMethod]
    public void Setting_Flags_Raises_PropertyChanged()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "PCScene" };
        using var node = new SceneNode(scene) { Name = "PCNode" };

        var changed = new List<string>();
        node.PropertyChanged += (_, e) => changed.Add(e.PropertyName!);

        // Act
        node.IsVisible = !node.IsVisible;
        node.CastsShadows = !node.CastsShadows;
        node.ReceivesShadows = !node.ReceivesShadows;
        node.IsRayCastingSelectable = !node.IsRayCastingSelectable;
        node.IgnoreParentTransform = !node.IgnoreParentTransform;
        node.IsStatic = !node.IsStatic;

        // Assert - ensure each property triggered a change
        _ = changed.Should().Contain(nameof(SceneNode.IsVisible));
        _ = changed.Should().Contain(nameof(SceneNode.CastsShadows));
        _ = changed.Should().Contain(nameof(SceneNode.ReceivesShadows));
        _ = changed.Should().Contain(nameof(SceneNode.IsRayCastingSelectable));
        _ = changed.Should().Contain(nameof(SceneNode.IgnoreParentTransform));
        _ = changed.Should().Contain(nameof(SceneNode.IsStatic));
    }
}
