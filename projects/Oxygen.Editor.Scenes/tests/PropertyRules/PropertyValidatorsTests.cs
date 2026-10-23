// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The single-property checks of the descriptors: each takes the range the scene schema generates for its property and
/// reports a value that is not finite, outside the range, or not a declared enumeration member with one of three codes.
/// </summary>
[TestClass]
public sealed class PropertyValidatorsTests
{
    public static IEnumerable<TestDataRow<ValidatorCase>> Rejected =>
    [
        Row("Position_NaN", SceneProperties.Transform.PositionXDescriptor, float.NaN, SceneDiagnosticCodes.PropertyValueNotFinite),
        Row("Rotation_Infinity", SceneProperties.Transform.RotationYDescriptor, float.PositiveInfinity, SceneDiagnosticCodes.PropertyValueNotFinite),
        Row("Scale_Zero", SceneProperties.Transform.ScaleZDescriptor, 0f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("Scale_NaN", SceneProperties.Transform.ScaleXDescriptor, float.NaN, SceneDiagnosticCodes.PropertyValueNotFinite),
        Row("PerspectiveNear_Zero", SceneProperties.PerspectiveCamera.NearPlaneDescriptor, 0f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("PerspectiveFar_NaN", SceneProperties.PerspectiveCamera.FarPlaneDescriptor, float.NaN, SceneDiagnosticCodes.PropertyValueNotFinite),
        Row("PerspectiveFieldOfView_AboveMax", SceneProperties.PerspectiveCamera.FieldOfViewDegreesDescriptor, 180f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("PerspectiveIso_Negative", SceneProperties.PerspectiveCamera.IsoDescriptor, -1f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("PerspectiveAspectMode_Undefined", SceneProperties.PerspectiveCamera.AspectModeDescriptor, (CameraAspectMode)99, SceneDiagnosticCodes.PropertyValueUndefined),
        Row("OrthographicSize_Zero", SceneProperties.OrthographicCamera.OrthographicSizeDescriptor, 0f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("OrthographicAperture_Zero", SceneProperties.OrthographicCamera.ApertureFDescriptor, 0f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("OrthographicAspectMode_Undefined", SceneProperties.OrthographicCamera.AspectModeDescriptor, (CameraAspectMode)99, SceneDiagnosticCodes.PropertyValueUndefined),
        Row("DirectionalColor_AboveOne", SceneProperties.DirectionalLight.ColorDescriptor, new Vector3(1, 1.5f, 1), SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("DirectionalColor_NaN", SceneProperties.DirectionalLight.ColorDescriptor, new Vector3(1, float.NaN, 1), SceneDiagnosticCodes.PropertyValueNotFinite),
        Row("DirectionalIntensity_Negative", SceneProperties.DirectionalLight.IntensityLuxDescriptor, -1f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("DirectionalCascadeCount_Five", SceneProperties.DirectionalLight.CascadeCountDescriptor, 5, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("DirectionalDiskScale_Negative", SceneProperties.DirectionalLight.AtmosphereDiskLuminanceScaleRgbDescriptor, new Vector3(-1, 1, 1), SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("DirectionalSplitMode_Undefined", SceneProperties.DirectionalLight.SplitModeDescriptor, (DirectionalCsmSplitMode)99, SceneDiagnosticCodes.PropertyValueUndefined),
        Row("PointFlux_Zero", SceneProperties.PointLight.LuminousFluxLumensDescriptor, 0f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("PointRange_Zero", SceneProperties.PointLight.RangeDescriptor, 0f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("PointExposure_AboveMax", SceneProperties.PointLight.ExposureCompensationDescriptor, 10.5f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("PointResolution_Undefined", SceneProperties.PointLight.ShadowResolutionHintDescriptor, (ShadowResolutionHint)99, SceneDiagnosticCodes.PropertyValueUndefined),
        Row("SpotCone_AboveRightAngle", SceneProperties.SpotLight.OuterConeAngleRadiansDescriptor!, 1.6f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("FogPhase_One", SceneProperties.LocalFogVolume.FogPhaseGDescriptor, 1f, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("FogSortPriority_AboveMax", SceneProperties.LocalFogVolume.SortPriorityDescriptor, 128, SceneDiagnosticCodes.PropertyValueOutOfRange),
        Row("FogAlbedo_Infinity", SceneProperties.LocalFogVolume.FogAlbedoDescriptor, new Vector3(float.PositiveInfinity, 0, 0), SceneDiagnosticCodes.PropertyValueNotFinite),
        Row("NodeVisibility_Undefined", SceneProperties.NodeFlags.VisibilityDescriptor, (VisibilitySource)99, SceneDiagnosticCodes.PropertyValueUndefined),
        Row("NodeCastsShadows_Undefined", SceneProperties.NodeFlags.CastsShadowsDescriptor, (ShadowSource)99, SceneDiagnosticCodes.PropertyValueUndefined),
    ];

    public static IEnumerable<TestDataRow<ValidatorCase>> Accepted =>
    [
        Row("Rotation_FullTurn", SceneProperties.Transform.RotationZDescriptor, 360f, string.Empty),
        Row("Scale_Negative", SceneProperties.Transform.ScaleYDescriptor, -2f, string.Empty),
        Row("PerspectiveFieldOfView_Min", SceneProperties.PerspectiveCamera.FieldOfViewDegreesDescriptor, 1f, string.Empty),
        Row("DirectionalColor_White", SceneProperties.DirectionalLight.ColorDescriptor, Vector3.One, string.Empty),
        Row("DirectionalCascadeCount_One", SceneProperties.DirectionalLight.CascadeCountDescriptor, 1, string.Empty),
        Row("FogPhase_Max", SceneProperties.LocalFogVolume.FogPhaseGDescriptor, 0.999f, string.Empty),
        Row("NodeVisibility_Inherit", SceneProperties.NodeFlags.VisibilityDescriptor, VisibilitySource.Inherit, string.Empty),
    ];

    [TestMethod]
    [DynamicData(nameof(Rejected))]
    public void Validate_ValueOutsideTheSchemaRange_ReportsTheGenericCode(ValidatorCase row)
    {
        ArgumentNullException.ThrowIfNull(row);

        var result = row.Descriptor.ValidateBoxed(row.Value);

        _ = result.IsValid.Should().BeFalse();
        _ = result.Code.Should().Be(row.Code);
    }

    [TestMethod]
    [DynamicData(nameof(Accepted))]
    public void Validate_ValueInTheSchemaRange_IsValid(ValidatorCase row)
    {
        ArgumentNullException.ThrowIfNull(row);

        _ = row.Descriptor.ValidateBoxed(row.Value).IsValid.Should().BeTrue();
    }

    [TestMethod]
    public void Validate_InspectorClampableProperty_StillRejectsOutOfRange_ClampingBelongsToTheCommit()
    {
        var fieldOfView = SceneProperties.PerspectiveCamera.FieldOfViewDegreesDescriptor;

        var committed = PropertyChecks.Commit(fieldOfView.Id, 200f);

        _ = fieldOfView.Validate(200f).Code.Should().Be(SceneDiagnosticCodes.PropertyValueOutOfRange);
        _ = fieldOfView.ValidateBoxed(committed).IsValid.Should().BeTrue();
    }

    [TestMethod]
    public void Validate_RejectingProperty_RejectsTheCommittedValueToo()
    {
        var nearPlane = SceneProperties.PerspectiveCamera.NearPlaneDescriptor;

        var committed = PropertyChecks.Commit(nearPlane.Id, 0f);

        _ = nearPlane.ValidateBoxed(committed).Code.Should().Be(SceneDiagnosticCodes.PropertyValueOutOfRange);
    }

    private static TestDataRow<ValidatorCase> Row<T>(string name, PropertyDescriptor<T> descriptor, T value, string code)
        => new(new ValidatorCase(descriptor, value, code)) { DisplayName = name };

    public sealed record ValidatorCase(PropertyDescriptor Descriptor, object? Value, string Code)
    {
        public override string ToString() => this.Descriptor.Id.Qualified();
    }
}
