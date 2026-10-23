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
    public void EveryOverridableValueType_HasAJsonContract()
        => _ = SceneSchema.Overridable.Values
            .Should().NotContain(static type => SceneJsonContext.Default.GetTypeInfo(type) == null, "an override value is read and written as its property's type");

    [TestMethod]
    [DataRow("""{"type":"fixed","lod_index":2}""")]
    [DataRow("""{"type":"distance","thresholds":[10,20],"hysteresis_ratio":0.1}""")]
    [DataRow("""{"type":"screen_space_error","enter_finer_sse":[4,2],"exit_coarser_sse":[3,1]}""")]

    // The engine's reports sort their keys, so the type follows the fields.
    [DataRow("""{"lod_index":2,"type":"fixed"}""")]
    [DataRow("""{"hysteresis_ratio":0.1,"thresholds":[10,20],"type":"distance"}""")]
    public void LodPolicy_RoundTripsItsSchemaJson(string json)
    {
        var policy = JsonSerializer.Deserialize(json, SceneJsonContext.Default.LodPolicy);

        var written = JsonSerializer.Serialize(policy, SceneJsonContext.Default.LodPolicy);

        using var source = JsonDocument.Parse(json);
        using var output = JsonDocument.Parse(written);
        _ = JsonSerializer.Deserialize(written, SceneJsonContext.Default.LodPolicy).Should().Be(policy);
        _ = output.RootElement.GetProperty("type").GetString().Should().Be(source.RootElement.GetProperty("type").GetString());
    }

    /// <summary>A policy the engine accepts has no problem; each broken rule is reported.</summary>
    [TestMethod]
    public void LodPolicy_FindsTheRuleItBreaks()
    {
        _ = new LodPolicy.Fixed(7).FindProblem().Should().BeNull();
        _ = new LodPolicy.Distance([10f, 10f, 30f], 0.99f).FindProblem().Should().BeNull();
        _ = new LodPolicy.ScreenSpaceError([120f, 40f], [100f, 30f]).FindProblem().Should().BeNull();

        _ = new LodPolicy.Fixed(8).FindProblem().Should().NotBeNull();
        _ = new LodPolicy.Distance([30f, 10f], 0.1f).FindProblem().Should().Contain("must not decrease");
        _ = new LodPolicy.Distance([10f, float.NaN], 0.1f).FindProblem().Should().Contain("finite");
        _ = new LodPolicy.Distance([10f], 1f).FindProblem().Should().Contain("hysteresis");
        _ = new LodPolicy.Distance([1f, 2f, 3f, 4f, 5f, 6f, 7f, 8f], 0.1f).FindProblem().Should().Contain("at most");
        _ = new LodPolicy.ScreenSpaceError([40f, 120f], [30f, 100f]).FindProblem().Should().Contain("must not increase");
        _ = new LodPolicy.ScreenSpaceError([120f, 40f], [100f]).FindProblem().Should().Contain("as many");
    }

    /// <summary>A saved LOD policy override the engine would refuse does not load.</summary>
    [TestMethod]
    public void InvalidLodPolicyOverride_DoesNotLoad()
    {
        using var policy = JsonDocument.Parse("""{ "type": "distance", "thresholds": [30, 10], "hysteresis_ratio": 0.1 }""");
        var saved = new SourceOverridesData
        {
            Values = new Dictionary<string, JsonElement>(StringComparer.Ordinal)
            {
                [SceneSchema.Geometry.LodPolicy.Id.JsonPointer] = policy.RootElement.Clone(),
            },
        };

        var read = () => SourceOverridesJson.Read(SceneSchema.Geometry.Kind, saved);

        _ = read.Should().Throw<JsonException>().WithMessage("*must not decrease*");
    }

    /// <summary>A LOD policy override keeps its type discriminator when saved, so it loads back as the same policy.</summary>
    [TestMethod]
    public void LodPolicyOverride_RoundTripsThroughSavedOverrides()
    {
        var overrides = SourceOverrides.For(SceneSchema.Geometry.Kind)
            .With(SceneSchema.Geometry.LodPolicy, (LodPolicy)new LodPolicy.Fixed(2));

        var saved = SourceOverridesJson.Write(overrides);
        var loaded = SourceOverridesJson.Read(SceneSchema.Geometry.Kind, saved);

        _ = saved!.Values![SceneSchema.Geometry.LodPolicy.Id.JsonPointer].GetProperty("type").GetString().Should().Be("fixed");
        _ = loaded.TryGet(SceneSchema.Geometry.LodPolicy, out var policy).Should().BeTrue();
        _ = policy.Should().Be(new LodPolicy.Fixed(2));
    }

    [TestMethod]
    public void LodPolicy_ComparesThresholdsByValue()
        => _ = new LodPolicy.Distance([10f, 20f], 0.1f).Should().Be(new LodPolicy.Distance([10f, 20f], 0.1f))
            .And.NotBe(new LodPolicy.Distance([10f, 30f], 0.1f));

    [TestMethod]
    public void EveryAssetTypedOverridableProperty_IsAReferenceField()
    {
        var assetProperties = SceneSchema.Overridable.Where(static entry => entry.Value == typeof(AssetPath)).Select(static entry => entry.Key).ToList();

        _ = assetProperties.Should().NotBeEmpty();
        foreach (var property in assetProperties)
        {
            _ = SceneReferenceFields.All.Should().Contain(
                field => field.Path.EndsWith("/Values/" + AssetReferenceField.Escape(property.JsonPointer), StringComparison.Ordinal),
                $"relocation must rewrite the asset of {property}");
        }
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

    [TestMethod]
    public async Task DeserializeAsync_NodeWithRemovedOverrideSlots_ThrowsJsonException()
    {
        var stream = new MemoryStream(Encoding.UTF8.GetBytes("""
            { "Id": "00000000-0000-4000-8000-000000000101", "Name": "Main", "RootNodes": [ { "Id": "00000000-0000-4000-8000-000000000201",
              "Components": [ { "$type": "Metadata", "Name": "Box" }, { "$type": "Transform", "Name": "Transform" },
                { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "Hierarchy", "Name": "Hierarchy" } ],
              "OverrideSlots": [] } ] }
            """));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>("a stale scene fails to open instead of being saved without what it held").ConfigureAwait(false);
    }
}
