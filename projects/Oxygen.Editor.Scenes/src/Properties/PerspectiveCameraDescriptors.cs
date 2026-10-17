// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for perspective camera inspector properties.
/// </summary>
public sealed class PerspectiveCameraDescriptors
{
    private PerspectiveCameraDescriptors(
        PropertyDescriptor<float> fieldOfViewDegrees,
        PropertyDescriptor<float> aspectRatio,
        PropertyDescriptor<float> nearPlane,
        PropertyDescriptor<float> farPlane,
        PropertyDescriptor<CameraAspectMode> aspectMode,
        PropertyDescriptor<float> apertureF,
        PropertyDescriptor<float> shutterRate,
        PropertyDescriptor<float> iso)
    {
        this.FieldOfViewDegrees = new PropertyId<float>(fieldOfViewDegrees.Id);
        this.AspectRatio = new PropertyId<float>(aspectRatio.Id);
        this.NearPlane = new PropertyId<float>(nearPlane.Id);
        this.FarPlane = new PropertyId<float>(farPlane.Id);
        this.FieldOfViewDegreesDescriptor = fieldOfViewDegrees;
        this.AspectRatioDescriptor = aspectRatio;
        this.NearPlaneDescriptor = nearPlane;
        this.FarPlaneDescriptor = farPlane;
        this.AspectModeDescriptor = aspectMode;
        this.ApertureFDescriptor = apertureF;
        this.ShutterRateDescriptor = shutterRate;
        this.IsoDescriptor = iso;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [aspectMode.Id] = aspectMode,
            [fieldOfViewDegrees.Id] = fieldOfViewDegrees,
            [aspectRatio.Id] = aspectRatio,
            [nearPlane.Id] = nearPlane,
            [farPlane.Id] = farPlane,
            [apertureF.Id] = apertureF,
            [shutterRate.Id] = shutterRate,
            [iso.Id] = iso,
        };
    }

    /// <summary>Gets the typed property id for field of view.</summary>
    public PropertyId<float> FieldOfViewDegrees { get; }

    /// <summary>Gets the typed property id for aspect ratio.</summary>
    public PropertyId<float> AspectRatio { get; }

    /// <summary>Gets the typed property id for the near clipping plane.</summary>
    public PropertyId<float> NearPlane { get; }

    /// <summary>Gets the typed property id for the far clipping plane.</summary>
    public PropertyId<float> FarPlane { get; }

    /// <summary>Gets the typed property id for the framing policy.</summary>
    public PropertyId<CameraAspectMode> AspectMode => new(this.AspectModeDescriptor.Id);

    /// <summary>Gets the typed property id for the aperture f-number.</summary>
    public PropertyId<float> ApertureF => new(this.ApertureFDescriptor.Id);

    /// <summary>Gets the typed property id for the shutter rate.</summary>
    public PropertyId<float> ShutterRate => new(this.ShutterRateDescriptor.Id);

    /// <summary>Gets the typed property id for the ISO sensitivity.</summary>
    public PropertyId<float> Iso => new(this.IsoDescriptor.Id);

    /// <summary>Gets the descriptor for the framing policy.</summary>
    public PropertyDescriptor<CameraAspectMode> AspectModeDescriptor { get; }

    /// <summary>Gets the descriptor for field of view.</summary>
    public PropertyDescriptor<float> FieldOfViewDegreesDescriptor { get; }

    /// <summary>Gets the descriptor for aspect ratio.</summary>
    public PropertyDescriptor<float> AspectRatioDescriptor { get; }

    /// <summary>Gets the descriptor for the near clipping plane.</summary>
    public PropertyDescriptor<float> NearPlaneDescriptor { get; }

    /// <summary>Gets the descriptor for the far clipping plane.</summary>
    public PropertyDescriptor<float> FarPlaneDescriptor { get; }

    /// <summary>Gets the descriptor for the aperture f-number.</summary>
    public PropertyDescriptor<float> ApertureFDescriptor { get; }

    /// <summary>Gets the descriptor for the shutter rate.</summary>
    public PropertyDescriptor<float> ShutterRateDescriptor { get; }

    /// <summary>Gets the descriptor for the ISO sensitivity.</summary>
    public PropertyDescriptor<float> IsoDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical perspective camera descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    public static PerspectiveCameraDescriptors Build()
        => new(
            fieldOfViewDegrees: FloatDescriptor(
                "/field_of_view_degrees",
                "Field of View",
                static camera => camera.FieldOfView,
                static (camera, value) => camera.FieldOfView = value,
                static value => !float.IsFinite(value)
                    ? ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")
                    : value is < 1f or > 179f
                        ? ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Field of view must be between 1 and 179 degrees.")
                        : ValidationResult.Ok,
                "perspective_camera.field_of_view_degrees"),
            aspectRatio: FloatDescriptor(
                "/aspect_ratio",
                "Aspect Ratio",
                static camera => camera.AspectRatio,
                static (camera, value) => camera.AspectRatio = value,
                static value => !float.IsFinite(value)
                    ? ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")
                    : value <= 0f
                        ? ValidationResult.Fail(SceneDiagnosticCodes.PerspectiveCameraAspectRatioNonPositive, "Aspect ratio must be greater than zero.")
                        : ValidationResult.Ok,
                "perspective_camera.aspect_ratio"),
            nearPlane: FloatDescriptor(
                "/near_plane",
                "Near Plane",
                static camera => camera.NearPlane,
                static (camera, value) => camera.NearPlane = value,
                static value => !float.IsFinite(value)
                    ? ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")
                    : value <= 0f
                        ? ValidationResult.Fail(SceneDiagnosticCodes.PerspectiveCameraNearPlaneNonPositive, "Near plane must be greater than zero.")
                        : ValidationResult.Ok,
                "perspective_camera.near_plane"),
            farPlane: FloatDescriptor(
                "/far_plane",
                "Far Plane",
                static camera => camera.FarPlane,
                static (camera, value) => camera.FarPlane = value,
                static value => float.IsFinite(value)
                    ? ValidationResult.Ok
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers."),
                "perspective_camera.far_plane"),
            aspectMode: new(
                id: new PropertyId<CameraAspectMode>(SceneProperties.PerspectiveCameraKind, "/aspect_mode"),
                reader: static target => ((PerspectiveCamera)target).AspectMode,
                writer: static (target, value) => ((PerspectiveCamera)target).AspectMode = value,
                validator: static value => Enum.IsDefined(value)
                    ? ValidationResult.Ok
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Unknown camera aspect mode."),
                annotation: SceneEditorSchemaAnnotations.Get(
                    "#/definitions/perspective_camera/aspect_mode",
                    new EditorAnnotation { Group = "Projection", Label = "Aspect Mode", Renderer = "combobox" }),
                engineCommandKey: "perspective_camera.aspect_mode")
            { Engine = SceneEngineFields.For(SceneProperties.PerspectiveCameraKind, "perspective_camera.aspect_mode") },
            apertureF: FloatDescriptor(
                "/aperture_f",
                "Aperture",
                static camera => camera.ApertureF,
                static (camera, value) => camera.ApertureF = value,
                Positive("Aperture must be greater than zero."),
                "perspective_camera.aperture_f"),
            shutterRate: FloatDescriptor(
                "/shutter_rate",
                "Shutter Speed",
                static camera => camera.ShutterRate,
                static (camera, value) => camera.ShutterRate = value,
                Positive("Shutter speed must be greater than zero."),
                "perspective_camera.shutter_rate"),
            iso: FloatDescriptor(
                "/iso",
                "ISO",
                static camera => camera.Iso,
                static (camera, value) => camera.Iso = value,
                Positive("ISO must be greater than zero."),
                "perspective_camera.iso"));

    private static Func<float, ValidationResult> Positive(string message)
        => value => !float.IsFinite(value)
            ? ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")
            : value <= 0f
                ? ValidationResult.Fail(SceneDiagnosticCodes.CameraExposureNonPositive, message)
                : ValidationResult.Ok;

    private static PropertyDescriptor<float> FloatDescriptor(
        string pointer,
        string label,
        Func<PerspectiveCamera, float> read,
        Action<PerspectiveCamera, float> write,
        Func<float, ValidationResult> validate,
        string engineCommandKey)
        => new(
            id: new PropertyId<float>(SceneProperties.PerspectiveCameraKind, pointer),
            reader: target => read((PerspectiveCamera)target),
            writer: (target, value) => write((PerspectiveCamera)target, value),
            validator: validate,
            annotation: SceneEditorSchemaAnnotations.Get(
                ToSceneSchemaPointer(engineCommandKey),
                new EditorAnnotation { Group = "Projection", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: engineCommandKey)
        { Engine = SceneEngineFields.For(SceneProperties.PerspectiveCameraKind, engineCommandKey) };

    private static string ToSceneSchemaPointer(string engineCommandKey)
        => engineCommandKey switch
        {
            "perspective_camera.field_of_view_degrees" => "#/definitions/perspective_camera/fov_y",
            "perspective_camera.aspect_ratio" => "#/definitions/perspective_camera/aspect_ratio",
            "perspective_camera.near_plane" => "#/definitions/perspective_camera/near_plane",
            "perspective_camera.far_plane" => "#/definitions/perspective_camera/far_plane",
            "perspective_camera.aperture_f" => "#/definitions/perspective_camera/aperture_f",
            "perspective_camera.shutter_rate" => "#/definitions/perspective_camera/shutter_rate",
            "perspective_camera.iso" => "#/definitions/perspective_camera/iso",
            _ => throw new ArgumentOutOfRangeException(nameof(engineCommandKey), engineCommandKey, "Unknown perspective camera command key."),
        };
}
