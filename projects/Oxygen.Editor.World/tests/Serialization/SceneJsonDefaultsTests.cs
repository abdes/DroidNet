// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// A scene record read from JSON takes its declared default for every value the JSON leaves out, as a record created in
/// code does.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public sealed class SceneJsonDefaultsTests
{
    private static readonly SceneId SceneIdentity = SceneId.FromValue(Guid.Parse("00000000-0000-4000-8000-000000000101"));
    private static readonly NodeId NodeIdentity = NodeId.FromValue(Guid.Parse("00000000-0000-4000-8000-000000000201"));

    [TestMethod]
    public void AbsentSceneValues_TakeTheDeclaredDefaults()
    {
        var read = JsonSerializer.Deserialize($$"""{ "Id": "{{SceneIdentity}}", "Name": "Scene" }""", SceneJsonContext.Default.SceneData)!;

        var expected = new SceneData { Id = SceneIdentity, Name = "Scene" };
        _ = JsonSerializer.Serialize(read, SceneJsonContext.Default.SceneData)
            .Should().Be(JsonSerializer.Serialize(expected, SceneJsonContext.Default.SceneData));
    }

    [TestMethod]
    public void AbsentNodeValues_TakeTheDeclaredDefaults()
    {
        var read = JsonSerializer.Deserialize($$"""{ "Id": "{{NodeIdentity}}" }""", SceneJsonContext.Default.SceneNodeData)!;

        _ = read.Components.Should().BeEmpty();
    }

    [TestMethod]
    [DataRow("Metadata")]
    [DataRow("Hierarchy")]
    [DataRow("Transform")]
    [DataRow("NodeFlags")]
    [DataRow("GeometryComponent")]
    [DataRow("PerspectiveCamera")]
    [DataRow("OrthographicCamera")]
    [DataRow("DirectionalLight")]
    [DataRow("PointLight")]
    [DataRow("SpotLight")]
    [DataRow("LocalFogVolume")]
    [DataRow("Scripting")]
    public void AbsentComponentValues_TakeTheDeclaredDefaults(string type)
    {
        var declared = Declared(type);
        var declaredJson = JsonSerializer.Serialize(declared, SceneJsonContext.Default.ComponentData);

        // The JSON keeps only the discriminator and the members the record requires.
        var minimal = new JsonObject();
        foreach (var (key, value) in JsonNode.Parse(declaredJson)!.AsObject())
        {
            if (key is "$type" or "Name" or "GeometryUri" or "AspectMode")
            {
                minimal[key] = value?.DeepClone();
            }
        }

        var read = JsonSerializer.Deserialize(minimal.ToJsonString(), SceneJsonContext.Default.ComponentData)!;

        // Every record draws a new identity, so the comparison takes the read one.
        _ = JsonSerializer.Serialize(read, SceneJsonContext.Default.ComponentData)
            .Should().Be(JsonSerializer.Serialize(declared with { Id = read.Id }, SceneJsonContext.Default.ComponentData));
    }

    [TestMethod]
    [DataRow("DirectionalLight")]
    [DataRow("PointLight")]
    [DataRow("SpotLight")]
    public void PartialShadowSettings_FailToLoadNamingTheMissingValue(string type)
    {
        var json = JsonNode.Parse(JsonSerializer.Serialize(Declared(type), SceneJsonContext.Default.ComponentData))!.AsObject();
        var shadow = json[nameof(LightComponentData.Shadow)]!.AsObject();
        _ = shadow.Remove(nameof(LightShadowSettingsData.NormalBias));

        var read = () => JsonSerializer.Deserialize(json.ToJsonString(), SceneJsonContext.Default.ComponentData);

        _ = read.Should().Throw<JsonException>().WithMessage($"*{nameof(LightShadowSettingsData.NormalBias)}*");
    }

    [TestMethod]
    [DataRow("DirectionalLight")]
    [DataRow("PointLight")]
    [DataRow("SpotLight")]
    public void AbsentShadowSettings_TakeTheKindsSchemaDefaults(string type)
    {
        var declared = (LightComponentData)Declared(type);
        var json = JsonNode.Parse(JsonSerializer.Serialize<ComponentData>(declared, SceneJsonContext.Default.ComponentData))!.AsObject();
        _ = json.Remove(nameof(LightComponentData.Shadow));

        var read = (LightComponentData)JsonSerializer.Deserialize(json.ToJsonString(), SceneJsonContext.Default.ComponentData)!;

        _ = read.Shadow.Should().Be(declared.Shadow);
    }

    private static ComponentData Declared(string type)
        => type switch
        {
            "Metadata" => new MetadataData { Name = "C" },
            "Hierarchy" => new HierarchyData { Name = "C" },
            "Transform" => new TransformData { Name = "C" },
            "NodeFlags" => new NodeFlagsData { Name = "C" },
            "GeometryComponent" => new GeometryComponentData { Name = "C", GeometryUri = AssetPath.Parse("/Engine/Generated/BasicShapes/Cube") },
            "PerspectiveCamera" => new PerspectiveCameraData { Name = "C", AspectMode = CameraAspectMode.Fixed },
            "OrthographicCamera" => new OrthographicCameraData { Name = "C", AspectMode = CameraAspectMode.Fixed },
            "DirectionalLight" => new DirectionalLightData { Name = "C" },
            "PointLight" => new PointLightData { Name = "C" },
            "SpotLight" => new SpotLightData { Name = "C" },
            "LocalFogVolume" => new LocalFogVolumeData { Name = "C" },
            "Scripting" => new ScriptingData { Name = "C" },
            _ => throw new ArgumentOutOfRangeException(nameof(type), type, "Not a scene component type."),
        };
}
