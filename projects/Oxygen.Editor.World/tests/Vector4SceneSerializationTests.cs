// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public sealed class Vector4SceneSerializationTests
{
    private static readonly JsonSerializerOptions VectorOptions = new() { Converters = { new Vector4JsonConverter() } };

    [TestMethod]
    public async Task DirectionalCascadeDistancesSurviveTheSceneJsonRoundTrip()
    {
        var project = Mock.Of<IProject>();
        var scene = new Scene(project) { Name = "Cascade serialization" };
        var node = new SceneNode(scene) { Name = "Sun" };
        var expected = new Vector4(16, 48, 128, 256);
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun", SplitMode = DirectionalCsmSplitMode.ManualDistances, CascadeDistances = expected });
        scene.RootNodes.Add(node);
        var serializer = new SceneSerializer(project);
        var stream = new MemoryStream();
        await using var streamLifetime = stream.ConfigureAwait(false);

        await serializer.SerializeAsync(stream, scene).ConfigureAwait(false);

        _ = Encoding.UTF8.GetString(stream.ToArray()).Should().Contain("\"W\": 256");
        stream.Position = 0;
        var reopened = await serializer.DeserializeAsync(stream).ConfigureAwait(false);
        var light = reopened.RootNodes.Single().Components.OfType<DirectionalLightComponent>().Single();
        _ = light.CascadeDistances.Should().Be(expected);
        _ = light.SplitMode.Should().Be(DirectionalCsmSplitMode.ManualDistances);
    }

    [TestMethod]
    [DataRow("{}", 0f, 0f, 0f, 0f)]
    [DataRow("{\"x\":1.25,\"Y\":2.5,\"z\":3.75,\"W\":4.5,\"extra\":{\"ignored\":true}}", 1.25f, 2.5f, 3.75f, 4.5f)]
    public void Vector4ObjectsFollowTheExistingVectorMemberConventions(string json, float x, float y, float z, float w)
    {
        var value = JsonSerializer.Deserialize<Vector4>(json, VectorOptions);

        _ = value.Should().Be(new Vector4(x, y, z, w));
    }
}
