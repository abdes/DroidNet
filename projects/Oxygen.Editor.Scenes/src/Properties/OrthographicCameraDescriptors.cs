// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for orthographic camera inspector properties.
/// </summary>
public sealed class OrthographicCameraDescriptors
{
    private OrthographicCameraDescriptors(
        PropertyDescriptor<float> orthographicSize,
        PropertyDescriptor<CameraAspectMode> aspectMode,
        PropertyDescriptor<float> aspectRatio,
        PropertyDescriptor<float> nearPlane,
        PropertyDescriptor<float> farPlane,
        PropertyDescriptor<float> apertureF,
        PropertyDescriptor<float> shutterRate,
        PropertyDescriptor<float> iso)
    {
        this.OrthographicSizeDescriptor = orthographicSize;
        this.AspectModeDescriptor = aspectMode;
        this.AspectRatioDescriptor = aspectRatio;
        this.NearPlaneDescriptor = nearPlane;
        this.FarPlaneDescriptor = farPlane;
        this.ApertureFDescriptor = apertureF;
        this.ShutterRateDescriptor = shutterRate;
        this.IsoDescriptor = iso;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [orthographicSize.Id] = orthographicSize,
            [aspectMode.Id] = aspectMode,
            [aspectRatio.Id] = aspectRatio,
            [nearPlane.Id] = nearPlane,
            [farPlane.Id] = farPlane,
            [apertureF.Id] = apertureF,
            [shutterRate.Id] = shutterRate,
            [iso.Id] = iso,
        };
    }

    /// <summary>Gets the typed property id for the orthographic size.</summary>
    public PropertyId<float> OrthographicSize => new(this.OrthographicSizeDescriptor.Id);

    /// <summary>Gets the typed property id for the framing policy.</summary>
    public PropertyId<CameraAspectMode> AspectMode => new(this.AspectModeDescriptor.Id);

    /// <summary>Gets the typed property id for the retained Fixed aspect ratio.</summary>
    public PropertyId<float> AspectRatio => new(this.AspectRatioDescriptor.Id);

    /// <summary>Gets the typed property id for the near clipping plane.</summary>
    public PropertyId<float> NearPlane => new(this.NearPlaneDescriptor.Id);

    /// <summary>Gets the typed property id for the far clipping plane.</summary>
    public PropertyId<float> FarPlane => new(this.FarPlaneDescriptor.Id);

    /// <summary>Gets the typed property id for the aperture f-number.</summary>
    public PropertyId<float> ApertureF => new(this.ApertureFDescriptor.Id);

    /// <summary>Gets the typed property id for the shutter rate.</summary>
    public PropertyId<float> ShutterRate => new(this.ShutterRateDescriptor.Id);

    /// <summary>Gets the typed property id for the ISO sensitivity.</summary>
    public PropertyId<float> Iso => new(this.IsoDescriptor.Id);

    /// <summary>Gets the descriptor for the orthographic size.</summary>
    public PropertyDescriptor<float> OrthographicSizeDescriptor { get; }

    /// <summary>Gets the descriptor for the framing policy.</summary>
    public PropertyDescriptor<CameraAspectMode> AspectModeDescriptor { get; }

    /// <summary>Gets the descriptor for the retained Fixed aspect ratio.</summary>
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

    /// <summary>Builds the canonical orthographic camera descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    public static OrthographicCameraDescriptors Build()
        => new(
            orthographicSize: FloatDescriptor(
                "/orthographic_size",
                "Orthographic Size",
                static camera => camera.OrthographicSize,
                static (camera, value) => camera.OrthographicSize = value,
                Positive(SceneDiagnosticCodes.OrthographicCameraSizeNonPositive, "Orthographic size must be greater than zero.")),
            aspectMode: new(
                id: new PropertyId<CameraAspectMode>(SceneProperties.OrthographicCameraKind, "/aspect_mode"),
                reader: static target => ((OrthographicCamera)target).AspectMode,
                writer: static (target, value) => ((OrthographicCamera)target).AspectMode = value,
                validator: static value => Enum.IsDefined(value)
                    ? ValidationResult.Ok
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Unknown camera aspect mode."),
                annotation: Annotation("aspect_mode", new EditorAnnotation { Group = "Projection", Label = "Aspect Mode", Renderer = "combobox" }),
                engineCommandKey: "orthographic_camera.aspect_mode") { Engine = SceneEngineFields.For(SceneProperties.OrthographicCameraKind, "orthographic_camera.aspect_mode") },
            aspectRatio: FloatDescriptor(
                "/aspect_ratio",
                "Fixed Aspect Ratio",
                static camera => camera.AspectRatio,
                static (camera, value) => camera.AspectRatio = value,
                Positive(SceneDiagnosticCodes.OrthographicCameraAspectRatioNonPositive, "Aspect ratio must be greater than zero.")),
            nearPlane: FloatDescriptor(
                "/near_plane",
                "Near Plane",
                static camera => camera.NearPlane,
                static (camera, value) => camera.NearPlane = value,
                Positive(SceneDiagnosticCodes.OrthographicCameraNearPlaneNonPositive, "Near plane must be greater than zero.")),
            farPlane: FloatDescriptor(
                "/far_plane",
                "Far Plane",
                static camera => camera.FarPlane,
                static (camera, value) => camera.FarPlane = value,
                static value => float.IsFinite(value)
                    ? ValidationResult.Ok
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")),
            apertureF: FloatDescriptor(
                "/aperture_f",
                "Aperture",
                static camera => camera.ApertureF,
                static (camera, value) => camera.ApertureF = value,
                Positive(SceneDiagnosticCodes.CameraExposureNonPositive, "Aperture must be greater than zero.")),
            shutterRate: FloatDescriptor(
                "/shutter_rate",
                "Shutter Speed",
                static camera => camera.ShutterRate,
                static (camera, value) => camera.ShutterRate = value,
                Positive(SceneDiagnosticCodes.CameraExposureNonPositive, "Shutter speed must be greater than zero.")),
            iso: FloatDescriptor(
                "/iso",
                "ISO",
                static camera => camera.Iso,
                static (camera, value) => camera.Iso = value,
                Positive(SceneDiagnosticCodes.CameraExposureNonPositive, "ISO must be greater than zero.")));

    private static Func<float, ValidationResult> Positive(string code, string message)
        => value => !float.IsFinite(value)
            ? ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")
            : value <= 0f
                ? ValidationResult.Fail(code, message)
                : ValidationResult.Ok;

    private static PropertyDescriptor<float> FloatDescriptor(
        string pointer,
        string label,
        Func<OrthographicCamera, float> read,
        Action<OrthographicCamera, float> write,
        Func<float, ValidationResult> validate)
        => new(
            id: new PropertyId<float>(SceneProperties.OrthographicCameraKind, pointer),
            reader: target => read((OrthographicCamera)target),
            writer: (target, value) => write((OrthographicCamera)target, value),
            validator: validate,
            annotation: Annotation(pointer.TrimStart('/'), new EditorAnnotation { Group = "Projection", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: "orthographic_camera." + pointer.TrimStart('/')) { Engine = SceneEngineFields.For(SceneProperties.OrthographicCameraKind, "orthographic_camera." + pointer.TrimStart('/')) };

    private static EditorAnnotation Annotation(string property, EditorAnnotation fallback)
        => SceneEditorSchemaAnnotations.Get("#/definitions/orthographic_camera/" + property, fallback);
}
