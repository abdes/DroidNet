// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// Typed ids must keep the scene file format: every id is written as the plain GUID string it was before.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public sealed class SceneIdentityJsonTests
{
    private static readonly SceneId Scene = SceneId.FromValue(Guid.Parse("00000000-0000-4000-8000-000000000101"));
    private static readonly NodeId Camera = NodeId.FromValue(Guid.Parse("00000000-0000-4000-8000-000000000201"));
    private static readonly ComponentId Transform = ComponentId.FromValue(Guid.Parse("00000000-0000-4000-8000-000000000301"));

    [TestMethod]
    public void IdsAreWrittenAsGuidStrings()
    {
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(CreateScene(), SceneJsonContext.Default.SceneData));
        var root = json.RootElement;

        _ = root.EnumerateObject().First().Name.Should().Be("Id");
        _ = root.GetProperty("Id").GetString().Should().Be(Scene.ToString());
        var node = root.GetProperty("RootNodes")[0];
        _ = node.EnumerateObject().First().Name.Should().Be("Id");
        _ = node.GetProperty("Id").GetString().Should().Be(Camera.ToString());
        _ = node.GetProperty("Components")[0].GetProperty("Id").GetString().Should().Be(Transform.ToString());
        _ = root.GetProperty("StartingCamera").GetString().Should().Be(Camera.ToString());
    }

    [TestMethod]
    public void IdsReadBackFromGuidStrings()
    {
        var json = JsonSerializer.Serialize(CreateScene(), SceneJsonContext.Default.SceneData);

        var data = JsonSerializer.Deserialize(json, SceneJsonContext.Default.SceneData)!;

        _ = data.Id.Should().Be(Scene);
        _ = data.RootNodes[0].Id.Should().Be(Camera);
        _ = data.RootNodes[0].Components[0].Id.Should().Be(Transform);
        _ = data.StartingCamera.Should().Be(Camera);
    }

    private static SceneData CreateScene() => new()
    {
        Id = Scene,
        Name = "Main",
        RootNodes =
        [
            new SceneNodeData
            {
                Id = Camera,
                Components =
                [
                    new TransformData { Id = Transform, Name = "Transform" },
                    new MetadataData { Name = "Camera" },
                    new NodeFlagsData { Name = "Rendering" },
                    new HierarchyData { Name = "Hierarchy" },
                ],
            },
        ],
        StartingCamera = Camera,
    };
}
