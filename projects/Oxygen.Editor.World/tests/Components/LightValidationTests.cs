// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies the light rules, which relate several values of a light, and the light ownership of a scene. A value outside
/// its own range is the component's to reject, before the rules see it.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(LightValidation))]
public sealed class LightValidationTests
{
    public static IEnumerable<TestDataRow<SingleValueCase>> OutOfRangeValues =>
    [
        Case("DirectionalColor_AboveOne", new DirectionalLightData { Name = "L", Color = new Vector3(0.5f, 1.5f, 0.5f) }, new Vector3(0.5f, 1.5f, 0.5f)),
        Case("DirectionalExposure_AboveMax", new DirectionalLightData { Name = "L", ExposureCompensation = 10.5f }, 10.5f),
        Case("DirectionalIntensity_Negative", new DirectionalLightData { Name = "L", IntensityLux = -1f }, -1f),
        Case("DirectionalAngularSize_AbovePi", new DirectionalLightData { Name = "L", AngularSizeRadians = 3.2f }, 3.2f),
        Case("DirectionalSlot_Undefined", new DirectionalLightData { Name = "L", AtmosphereSlot = (AtmosphereLightSlot)99 }, (AtmosphereLightSlot)99),
        Case("DirectionalCascadeCount_Five", new DirectionalLightData { Name = "L", CascadeCount = 5 }, 5),
        Case("DirectionalSplitMode_Undefined", new DirectionalLightData { Name = "L", SplitMode = (DirectionalCsmSplitMode)99 }, (DirectionalCsmSplitMode)99),
        Case("DirectionalMaxShadowDistance_Zero", new DirectionalLightData { Name = "L", MaxShadowDistance = 0f }, 0f),
        Case("DirectionalDistribution_BelowOne", new DirectionalLightData { Name = "L", DistributionExponent = 0.5f }, 0.5f),
        Case("DirectionalTransition_AboveOne", new DirectionalLightData { Name = "L", TransitionFraction = 1.5f }, 1.5f),
        Case("DirectionalFadeout_Negative", new DirectionalLightData { Name = "L", DistanceFadeoutFraction = -0.1f }, -0.1f),
        Case("DirectionalDiskScale_Negative", new DirectionalLightData { Name = "L", AtmosphereDiskLuminanceScaleRgb = new Vector3(1, -1, 1) }, new Vector3(1, -1, 1)),
        Case("DirectionalCascadeDistance_NotFinite", new DirectionalLightData { Name = "L", CascadeDistances = new Vector4(8, 24, 64, float.NaN) }, float.NaN),
        Case("PointFlux_Zero", new PointLightData { Name = "L", LuminousFluxLumens = 0f }, 0f),
        Case("PointRange_Zero", new PointLightData { Name = "L", Range = 0f }, 0f),
        Case("PointSourceRadius_Negative", new PointLightData { Name = "L", SourceRadius = -1f }, -1f),
        Case("PointShadowBias_AboveMax", new PointLightData { Name = "L", Shadow = Shadow(bias: 11f) }, 11f),
        Case("PointResolutionHint_Undefined", new PointLightData { Name = "L", Shadow = Shadow(hint: (ShadowResolutionHint)99) }, (ShadowResolutionHint)99),
        Case("SpotOuterCone_AboveRightAngle", new SpotLightData { Name = "L", OuterConeAngleRadians = 1.6f }, 1.6f),
        Case("SpotInnerCone_Negative", new SpotLightData { Name = "L", InnerConeAngleRadians = -0.1f }, -0.1f),
        Case("SpotColor_Negative", new SpotLightData { Name = "L", Color = new Vector3(-1, 0, 0) }, new Vector3(-1, 0, 0)),
    ];

    public static IEnumerable<TestDataRow<RuleCase>> InconsistentValues =>
    [
        Rule("SpotInnerAboveOuter", new SpotLightData { Name = "L", InnerConeAngleRadians = 0.8f, OuterConeAngleRadians = 0.4f }, LightRule.SpotConeInvalid, SceneSchema.SpotLight.InnerConeAngleRadians.Id),
        Rule("SpotOuterZero", new SpotLightData { Name = "L", InnerConeAngleRadians = 0f, OuterConeAngleRadians = 0f }, LightRule.SpotConeInvalid, SceneSchema.SpotLight.OuterConeAngleRadians.Id),
        Rule("DirectionalCascadesNotIncreasing", new DirectionalLightData { Name = "L", CascadeCount = 3, CascadeDistances = new Vector4(5, 4, 40, 90) }, LightRule.CascadeDistancesNotIncreasing, SceneSchema.DirectionalLight.CascadeDistance1.Id),
        Rule("PointIntensityTooSmall", new PointLightData { Name = "L", LuminousFluxLumens = 1e-35f, ExposureCompensation = -10f }, LightRule.IntensityNotRepresentable, SceneSchema.PointLight.LuminousFluxLumens.Id),
    ];

    [TestMethod]
    public void Validate_DefaultLights_KeepEveryRule()
    {
        _ = LightValidation.Validate(new DirectionalLightData { Name = "L" }).Should().BeNull();
        _ = LightValidation.Validate(new PointLightData { Name = "L" }).Should().BeNull();
        _ = LightValidation.Validate(new SpotLightData { Name = "L" }).Should().BeNull();
    }

    [TestMethod]
    [DynamicData(nameof(OutOfRangeValues))]
    public void Hydrate_ValueOutsideTheSchemaRange_IsRejectedAsThatValue(SingleValueCase row)
    {
        ArgumentNullException.ThrowIfNull(row);

        var create = () => GameComponent.CreateAndHydrate(row.Light);

        _ = create.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(row.Value);
    }

    [TestMethod]
    [DynamicData(nameof(InconsistentValues))]
    public void Validate_ValuesInconsistentTogether_ReportTheRuleAndItsProperties(RuleCase row)
    {
        ArgumentNullException.ThrowIfNull(row);

        var violation = LightValidation.Validate(row.Light);

        _ = violation.Should().NotBeNull();
        _ = violation!.Rule.Should().Be(row.Rule);
        _ = violation.Properties.Should().Contain(row.Property);
    }

    [TestMethod]
    [DynamicData(nameof(InconsistentValues))]
    public void Hydrate_ValuesInconsistentTogether_AreRejected(RuleCase row)
    {
        ArgumentNullException.ThrowIfNull(row);

        var create = () => GameComponent.CreateAndHydrate(row.Light);

        _ = create.Should().Throw<ArgumentException>().Which.Should().NotBeOfType<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void Hydrate_ValueOutsideTheSchemaRange_ThrowsWithoutMutation()
    {
        var light = new PointLightComponent { Name = "Accepted" };

        Action hydrate = () => light.Hydrate(new PointLightData { Name = "Rejected", Range = 0f });

        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>();
        _ = light.Name.Should().Be("Accepted");
        _ = light.Range.Should().Be(SceneSchema.PointLight.Defaults.Range);
    }

    [TestMethod]
    public void ValidateScene_WhenNodeHasTwoLights_ReturnsErrorOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "MultiLightNode");
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
        _ = node.AddComponent(new PointLightComponent { Name = "Point" });
        scene.RootNodes.Add(node);

        // The record holds what the scene model refuses to write, as a hand-edited file could.
        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes = [SceneTestSupport.NodeData("MultiLightNode", new DirectionalLightData { Name = "Sun" }, new PointLightData { Name = "Point" })],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_WhenTwoDirectionalLightsClaimSameAtmosphereSlot_ReturnsErrorOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node1 = new SceneNode(scene, "Sun1");
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene, "Sun2");
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // The record holds what the scene model refuses to write, as a hand-edited file could.
        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                SceneTestSupport.NodeData("Sun1", new DirectionalLightData { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary }),
                SceneTestSupport.NodeData("Sun2", new DirectionalLightData { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary }),
            ],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_InvisibleNodesWithConflictingLights_StillCountOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node1 = new SceneNode(scene, "InvisibleSun");
        node1.Flags.Visibility = VisibilitySource.Hidden;
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene, "VisibleSun");
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // The record holds what the scene model refuses to write, as a hand-edited file could.
        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Components =
                    [
                        new MetadataData { Name = "InvisibleSun" },
                        new TransformData { Name = "Transform" },
                        new NodeFlagsData { Name = NodeFlagsComponent.DefaultName, Visibility = VisibilitySource.Hidden },
                        new DirectionalLightData { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary },
                        new HierarchyData { Name = HierarchyComponent.DefaultName },
                    ],
                },
                SceneTestSupport.NodeData("VisibleSun", new DirectionalLightData { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary }),
            ],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_SceneAndSceneDataOverloads_AgreeOnValidScene()
    {
        var scene = new Scene { Name = "Valid Scene" };
        var node1 = new SceneNode(scene, "Sun");
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene, "Moon");
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Moon", AtmosphereSlot = AtmosphereLightSlot.Secondary });
        var node3 = new SceneNode(scene, "Torch");
        _ = node3.AddComponent(new PointLightComponent { Name = "Torch" });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);
        scene.RootNodes.Add(node3);

        var sceneData = scene.Dehydrate();

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().BeNull();
        _ = dataError.Should().BeNull();
    }

    private static TestDataRow<SingleValueCase> Case(string name, LightComponentData light, object value)
        => new(new SingleValueCase(light, value)) { DisplayName = name };

    private static TestDataRow<RuleCase> Rule(string name, LightComponentData light, LightRule rule, PropertyId property)
        => new(new RuleCase(light, rule, property)) { DisplayName = name };

    private static LightShadowSettingsData Shadow(float bias = 0.001f, ShadowResolutionHint hint = ShadowResolutionHint.Medium)
        => new() { Bias = bias, NormalBias = 0.02f, ContactShadows = false, ResolutionHint = hint };

    /// <summary>A light with one value outside the range the schema declares, and that value.</summary>
    /// <param name="Light">The light.</param>
    /// <param name="Value">The rejected value.</param>
    public sealed record SingleValueCase(LightComponentData Light, object Value);

    /// <summary>A light whose values, each in range, break a light rule together.</summary>
    /// <param name="Light">The light.</param>
    /// <param name="Rule">The broken rule.</param>
    /// <param name="Property">One of the properties the rule relates.</param>
    public sealed record RuleCase(LightComponentData Light, LightRule Rule, PropertyId Property);
}
