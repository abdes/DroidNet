// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Tests.Utils;

/// <summary>
/// Verifies Vector4 serialization, member conventions, and scene round-tripping.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Vector4JsonConverter))]
public sealed class Vector4SceneSerializationTests
{
    private static readonly JsonSerializerOptions VectorOptions = new() { Converters = { new Vector4JsonConverter() } };

    [TestMethod]
    public async Task DirectionalCascadeDistancesSurviveTheSceneJsonRoundTrip()
    {
        var scene = new Scene { Name = "Cascade serialization" };
        var node = new SceneNode(scene, "Sun");
        var expected = new Vector4(16, 48, 128, 256);
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun", SplitMode = DirectionalCsmSplitMode.ManualDistances, CascadeDistances = expected });
        scene.RootNodes.Add(node);

        var stream = await SceneTestSupport.SerializeToStreamAsync(scene).ConfigureAwait(false);
        await using var streamLifetime = stream.ConfigureAwait(false);

        _ = Encoding.UTF8.GetString(stream.ToArray()).Should().Contain("\"W\": 256");
        var reopened = await SceneTestSupport.DeserializeFromStreamAsync(stream).ConfigureAwait(false);
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

    [TestMethod]
    [DataRow("[]")]
    [DataRow("\"not-an-object\"")]
    public void Vector3JsonConverter_WhenInvalidJsonToken_ThrowsJsonException(string json)
    {
        var options = new JsonSerializerOptions { Converters = { new Vector3JsonConverter() } };
        Action act = () => JsonSerializer.Deserialize<Vector3>(json, options);
        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    [DataRow("[]")]
    [DataRow("\"not-an-object\"")]
    public void QuaternionJsonConverter_WhenInvalidJsonToken_ThrowsJsonException(string json)
    {
        var options = new JsonSerializerOptions { Converters = { new QuaternionJsonConverter() } };
        Action act = () => JsonSerializer.Deserialize<Quaternion>(json, options);
        _ = act.Should().Throw<JsonException>();
    }
}
