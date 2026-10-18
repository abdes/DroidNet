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
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// Verifies the structural JSON contract of scene files including discriminators, vector encodings, and malformed JSON resilience.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public sealed class SceneJsonFormatTests
{
    [TestMethod]
    [DataRow("PerspectiveCamera")]
    [DataRow("OrthographicCamera")]
    [DataRow("DirectionalLight")]
    [DataRow("PointLight")]
    [DataRow("SpotLight")]
    [DataRow("LocalFogVolume")]
    [DataRow("Scripting")]
    public void ComponentData_SerializesExpectedTypeDiscriminator(string componentType)
    {
        ComponentData instance = componentType switch
        {
            "PerspectiveCamera" => new PerspectiveCameraData { Name = "Camera", AspectMode = CameraAspectMode.Fixed },
            "OrthographicCamera" => new OrthographicCameraData { Name = "Camera", AspectMode = CameraAspectMode.Fixed },
            "DirectionalLight" => new DirectionalLightData { Name = "Light" },
            "PointLight" => new PointLightData { Name = "Light" },
            "SpotLight" => new SpotLightData { Name = "Light" },
            "LocalFogVolume" => new LocalFogVolumeData { Name = "Fog" },
            "Scripting" => new ScriptingData { Name = "Scripting" },
            _ => throw new ArgumentException("Unknown component type", nameof(componentType)),
        };

        var json = JsonSerializer.Serialize(instance, SceneJsonContext.Default.ComponentData);

        _ = json.Should().Contain($"\"$type\": \"{componentType}\"");
    }

    [TestMethod]
    [DataRow("MaterialsSlot")]
    [DataRow("RenderingSlot")]
    [DataRow("LightingSlot")]
    [DataRow("LevelOfDetailSlot")]
    public void OverrideSlotData_SerializesExpectedTypeDiscriminator(string slotType)
    {
        OverrideSlotData instance = slotType switch
        {
            "MaterialsSlot" => new MaterialsSlotData
            {
                GeometryUri = new Uri("asset:///Mesh.geometry"),
                SlotId = Guid.NewGuid(),
                LayoutRevision = "rev1",
                MaterialUri = new Uri("asset:///Material.omat.json"),
            },
            "RenderingSlot" => new RenderingSlotData(),
            "LightingSlot" => new LightingSlotData(),
            "LevelOfDetailSlot" => new LevelOfDetailSlotData(),
            _ => throw new ArgumentException("Unknown slot type", nameof(slotType)),
        };

        var json = JsonSerializer.Serialize(instance, SceneJsonContext.Default.OverrideSlotData);

        _ = json.Should().Contain($"\"$type\": \"{slotType}\"");
    }

    [TestMethod]
    public void TransformData_SerializesVectorsAndQuaternionsAsNamedObjects()
    {
        var transform = new TransformData
        {
            Name = "Transform",
            Position = new Vector3(1.5f, 2.5f, 3.5f),
            Rotation = new Quaternion(0f, 0f, 0f, 1f),
            Scale = new Vector3(1f, 1f, 1f),
        };

        var json = JsonSerializer.Serialize(transform, SceneJsonContext.Default.TransformData);

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        var pos = root.GetProperty("LocalPosition");
        _ = pos.GetProperty("X").GetSingle().Should().Be(1.5f);
        _ = pos.GetProperty("Y").GetSingle().Should().Be(2.5f);
        _ = pos.GetProperty("Z").GetSingle().Should().Be(3.5f);

        var rot = root.GetProperty("LocalRotation");
        _ = rot.GetProperty("X").GetSingle().Should().Be(0f);
        _ = rot.GetProperty("Y").GetSingle().Should().Be(0f);
        _ = rot.GetProperty("Z").GetSingle().Should().Be(0f);
        _ = rot.GetProperty("W").GetSingle().Should().Be(1f);

        var scale = root.GetProperty("LocalScale");
        _ = scale.GetProperty("X").GetSingle().Should().Be(1f);
        _ = scale.GetProperty("Y").GetSingle().Should().Be(1f);
        _ = scale.GetProperty("Z").GetSingle().Should().Be(1f);
    }

    [TestMethod]
    public async Task DeserializeAsync_WhenJsonSyntaxInvalid_ThrowsJsonException()
    {
        var stream = new MemoryStream(Encoding.UTF8.GetBytes("{ not valid json }"));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task DeserializeAsync_WhenTruncatedAfterPropertyName_ThrowsJsonException()
    {
        var stream = new MemoryStream(Encoding.UTF8.GetBytes("{\"Name\":"));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }
}
