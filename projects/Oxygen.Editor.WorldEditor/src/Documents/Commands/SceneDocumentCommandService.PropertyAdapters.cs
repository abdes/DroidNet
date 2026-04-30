// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Property-edit adapters for component commands that still own specialized sync and side effects.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    private static string? GetSingleComponentKind(PropertyEdit edit)
    {
        string? kind = null;
        foreach (var id in edit.Ids)
        {
            if (kind is null)
            {
                kind = id.ComponentKind;
                continue;
            }

            if (!string.Equals(kind, id.ComponentKind, StringComparison.Ordinal))
            {
                return null;
            }
        }

        return kind;
    }

    private static string OperationKindForPropertyKind(string kind)
        => kind switch
        {
            GeometryKind => SceneOperationKinds.EditGeometry,
            PerspectiveCameraKind => SceneOperationKinds.EditPerspectiveCamera,
            DirectionalLightKind => SceneOperationKinds.EditDirectionalLight,
            SceneEnvironmentKind => SceneOperationKinds.EditEnvironment,
            _ => SceneOperationKinds.EditTransform,
        };

    private static GeometryEdit BuildGeometryEditFromPropertyEdit(PropertyEdit edit)
        => new(edit.Contains(Geometry.GeometryUri.Id)
            ? OptionalEditValues.Supplied<Uri?>(GetNullableReference(edit, Geometry.GeometryUri))
            : OptionalEditValues.Unspecified<Uri?>());

    private static PerspectiveCameraEdit BuildPerspectiveCameraEditFromPropertyEdit(PropertyEdit edit)
        => new(
            GetOptional(edit, PerspectiveCamera.FieldOfViewDegrees),
            GetOptional(edit, PerspectiveCamera.AspectRatio),
            GetOptional(edit, PerspectiveCamera.NearPlane),
            GetOptional(edit, PerspectiveCamera.FarPlane));

    private static DirectionalLightEdit BuildDirectionalLightEditFromPropertyEdit(PropertyEdit edit)
        => new(
            GetOptional(edit, DirectionalLight.Color),
            GetOptional(edit, DirectionalLight.IntensityLux),
            GetOptional(edit, DirectionalLight.IsSunLight),
            GetOptional(edit, DirectionalLight.EnvironmentContribution),
            GetOptional(edit, DirectionalLight.CastsShadows),
            GetOptional(edit, DirectionalLight.AffectsWorld),
            GetOptional(edit, DirectionalLight.AngularSizeRadians),
            GetOptional(edit, DirectionalLight.ExposureCompensation),
            GetOptional(edit, DirectionalLight.Mobility),
            GetOptional(edit, DirectionalLight.ShadowBias),
            GetOptional(edit, DirectionalLight.ShadowNormalBias),
            GetOptional(edit, DirectionalLight.ContactShadows),
            GetOptional(edit, DirectionalLight.ShadowResolutionHint),
            GetOptional(edit, DirectionalLight.CascadeCount),
            GetOptional(edit, DirectionalLight.SplitMode),
            GetOptional(edit, DirectionalLight.MaxShadowDistance),
            GetOptional(edit, DirectionalLight.CascadeDistance0),
            GetOptional(edit, DirectionalLight.CascadeDistance1),
            GetOptional(edit, DirectionalLight.CascadeDistance2),
            GetOptional(edit, DirectionalLight.CascadeDistance3),
            GetOptional(edit, DirectionalLight.DistributionExponent),
            GetOptional(edit, DirectionalLight.TransitionFraction),
            GetOptional(edit, DirectionalLight.DistanceFadeoutFraction));

    private static TransformEdit BuildTransformEditFromPropertyEdit(PropertyEdit edit)
        => new(
            OptionalEditValues.Unspecified<Vector3>(),
            OptionalEditValues.Unspecified<Vector3>(),
            OptionalEditValues.Unspecified<Vector3>(),
            PositionX: GetOptional(edit, Transform.PositionX),
            PositionY: GetOptional(edit, Transform.PositionY),
            PositionZ: GetOptional(edit, Transform.PositionZ),
            RotationXDegrees: GetOptional(edit, Transform.RotationX),
            RotationYDegrees: GetOptional(edit, Transform.RotationY),
            RotationZDegrees: GetOptional(edit, Transform.RotationZ),
            ScaleX: GetOptional(edit, Transform.ScaleX),
            ScaleY: GetOptional(edit, Transform.ScaleY),
            ScaleZ: GetOptional(edit, Transform.ScaleZ));

    private static OptionalEditValue<T> GetOptional<T>(PropertyEdit edit, PropertyId<T> id)
        => edit.GetTyped(id, out var value) ? OptionalEditValues.Supplied<T>(value) : OptionalEditValues.Unspecified<T>();

    private static T? GetNullableReference<T>(PropertyEdit edit, PropertyId<T?> id)
        where T : class
    {
        return edit.TryGetRaw(id.Id, out var value)
            ? (T?)value
            : null;
    }

    private async Task<SceneCommandResult> EditComponentPropertiesThroughExistingCommandAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PropertyEdit edit,
        string kind,
        EditSessionToken session)
    {
        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        if (this.ValidateComponentPropertyEdit(context, edit, kind) is { } validationResult)
        {
            return validationResult;
        }

        return kind switch
        {
            GeometryKind => await this.EditGeometryPropertiesAsync(context, nodeIds, edit, session).ConfigureAwait(true),
            PerspectiveCameraKind => await this.EditPerspectiveCameraAsync(
                context,
                nodeIds,
                BuildPerspectiveCameraEditFromPropertyEdit(edit),
                session).ConfigureAwait(true),
            DirectionalLightKind => await this.EditDirectionalLightAsync(
                context,
                nodeIds,
                BuildDirectionalLightEditFromPropertyEdit(edit),
                session).ConfigureAwait(true),
            _ => this.ValidationFailure(
                SceneOperationKinds.EditTransform,
                "PROPERTY_UNKNOWN",
                "Property edit rejected",
                $"Unknown component kind: {kind}.",
                context),
        };
    }

    private SceneCommandResult? ValidateComponentPropertyEdit(
        SceneDocumentCommandContext context,
        PropertyEdit edit,
        string kind)
    {
        var descriptors = kind switch
        {
            TransformKind => Transform.ById,
            GeometryKind => Geometry.ById,
            PerspectiveCameraKind => PerspectiveCamera.ById,
            DirectionalLightKind => DirectionalLight.ById,
            _ => null,
        };

        if (descriptors is null)
        {
            return null;
        }

        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                return this.ValidationFailure(
                    OperationKindForPropertyKind(kind),
                    "PROPERTY_UNKNOWN",
                    "Property edit rejected",
                    $"Unknown property id: {id.Qualified()}.",
                    context);
            }

            var validation = descriptor.ValidateBoxed(value);
            if (!validation.IsValid)
            {
                return this.ValidationFailure(
                    OperationKindForPropertyKind(kind),
                    validation.Code,
                    "Property edit rejected",
                    validation.Message,
                    context);
            }
        }

        return null;
    }

    private async Task<SceneCommandResult> EditGeometryPropertiesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PropertyEdit edit,
        EditSessionToken session)
    {
        var result = SceneCommandResult.Success;
        if (edit.Contains(Geometry.GeometryUri.Id))
        {
            result = await this.EditGeometryAsync(
                context,
                nodeIds,
                BuildGeometryEditFromPropertyEdit(edit),
                session).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                return result;
            }
        }

        if (edit.Contains(Geometry.MaterialSlot0Uri.Id))
        {
            var materialUri = GetNullableReference(edit, Geometry.MaterialSlot0Uri);
            result = await this.EditMaterialSlotAsync(
                context,
                nodeIds,
                slotIndex: 0,
                materialUri,
                session).ConfigureAwait(true);
        }

        return result;
    }

    private SceneEnvironmentEditConversion TryBuildSceneEnvironmentEdit(
        SceneDocumentCommandContext context,
        PropertyEdit edit)
    {
        foreach (var (id, value) in edit)
        {
            if (!SceneEnvironment.ById.TryGetValue(id, out var descriptor))
            {
                return new SceneEnvironmentEditConversion(EmptyEnvironmentEdit(), this.ValidationFailure(
                    SceneOperationKinds.EditEnvironment,
                    "PROPERTY_UNKNOWN",
                    "Environment edit rejected",
                    $"Unknown property id: {id.Qualified()}.",
                    context));
            }

            var validation = descriptor.ValidateBoxed(value);
            if (!validation.IsValid)
            {
                return new SceneEnvironmentEditConversion(EmptyEnvironmentEdit(), this.ValidationFailure(
                    SceneOperationKinds.EditEnvironment,
                    validation.Code,
                    "Environment edit rejected",
                    validation.Message,
                    context));
            }
        }

        var target = new SceneEnvironmentPropertyTarget(context.Scene.Environment);
        PropertyApply.ApplyToTarget(target, edit, SceneEnvironment.ById);
        var after = target.Value;
        var postProcessEdited = edit.Ids.Any(static id => id.Pointer.StartsWith("/post_process/", StringComparison.Ordinal));
        var skyEdited = edit.Ids.Any(static id => id.Pointer.StartsWith("/sky_atmosphere/", StringComparison.Ordinal));

        var atmosphereEnabled = edit.Contains(SceneEnvironment.AtmosphereEnabled.Id)
            ? OptionalEditValues.Supplied<bool>(after.AtmosphereEnabled)
            : OptionalEditValues.Unspecified<bool>();
        var sunNodeId = edit.Contains(SceneEnvironment.SunNodeId.Id)
            ? OptionalEditValues.Supplied<Guid?>(after.SunNodeId)
            : OptionalEditValues.Unspecified<Guid?>();
        var backgroundColor = edit.Contains(SceneEnvironment.BackgroundColor.Id)
            ? OptionalEditValues.Supplied<Vector3>(after.BackgroundColor)
            : OptionalEditValues.Unspecified<Vector3>();
        var skyAtmosphere = skyEdited
            ? OptionalEditValues.Supplied<SkyAtmosphereEnvironmentData>(after.SkyAtmosphere)
            : OptionalEditValues.Unspecified<SkyAtmosphereEnvironmentData>();
        var postProcess = postProcessEdited
            ? OptionalEditValues.Supplied<PostProcessEnvironmentData>(after.PostProcess)
            : OptionalEditValues.Unspecified<PostProcessEnvironmentData>();

        return new SceneEnvironmentEditConversion(
            new SceneEnvironmentEdit(
                atmosphereEnabled,
                sunNodeId,
                OptionalEditValues.Unspecified<ExposureMode>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                backgroundColor,
                skyAtmosphere,
                postProcess),
            null);

        static SceneEnvironmentEdit EmptyEnvironmentEdit()
            => new(
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<Guid?>(),
                OptionalEditValues.Unspecified<ExposureMode>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                OptionalEditValues.Unspecified<Vector3>());
    }

    /// <summary>
    /// Mutable property target used while projecting scene environment property edits.
    /// </summary>
    internal sealed class SceneEnvironmentPropertyTarget(SceneEnvironmentData value)
    {
        /// <summary>
        /// Gets or sets the projected scene environment value.
        /// </summary>
        public SceneEnvironmentData Value { get; set; } = value;
    }

    private sealed record SceneEnvironmentEditConversion(SceneEnvironmentEdit Edit, SceneCommandResult? Result);
}
