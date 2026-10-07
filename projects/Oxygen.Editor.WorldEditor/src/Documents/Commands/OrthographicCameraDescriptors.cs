// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for orthographic camera inspector properties.
/// </summary>
internal sealed class OrthographicCameraDescriptors
{
    private OrthographicCameraDescriptors(
        PropertyDescriptor<float> orthographicSize,
        PropertyDescriptor<CameraAspectMode> aspectMode,
        PropertyDescriptor<float> aspectRatio,
        PropertyDescriptor<float> nearPlane,
        PropertyDescriptor<float> farPlane)
    {
        this.OrthographicSizeDescriptor = orthographicSize;
        this.AspectModeDescriptor = aspectMode;
        this.AspectRatioDescriptor = aspectRatio;
        this.NearPlaneDescriptor = nearPlane;
        this.FarPlaneDescriptor = farPlane;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [orthographicSize.Id] = orthographicSize,
            [aspectMode.Id] = aspectMode,
            [aspectRatio.Id] = aspectRatio,
            [nearPlane.Id] = nearPlane,
            [farPlane.Id] = farPlane,
        };
    }

    /// <summary>Gets the typed property id for the orthographic size.</summary>
    internal PropertyId<float> OrthographicSize => new(this.OrthographicSizeDescriptor.Id);

    /// <summary>Gets the typed property id for the framing policy.</summary>
    internal PropertyId<CameraAspectMode> AspectMode => new(this.AspectModeDescriptor.Id);

    /// <summary>Gets the typed property id for the retained Fixed aspect ratio.</summary>
    internal PropertyId<float> AspectRatio => new(this.AspectRatioDescriptor.Id);

    /// <summary>Gets the typed property id for the near clipping plane.</summary>
    internal PropertyId<float> NearPlane => new(this.NearPlaneDescriptor.Id);

    /// <summary>Gets the typed property id for the far clipping plane.</summary>
    internal PropertyId<float> FarPlane => new(this.FarPlaneDescriptor.Id);

    /// <summary>Gets the descriptor for the orthographic size.</summary>
    internal PropertyDescriptor<float> OrthographicSizeDescriptor { get; }

    /// <summary>Gets the descriptor for the framing policy.</summary>
    internal PropertyDescriptor<CameraAspectMode> AspectModeDescriptor { get; }

    /// <summary>Gets the descriptor for the retained Fixed aspect ratio.</summary>
    internal PropertyDescriptor<float> AspectRatioDescriptor { get; }

    /// <summary>Gets the descriptor for the near clipping plane.</summary>
    internal PropertyDescriptor<float> NearPlaneDescriptor { get; }

    /// <summary>Gets the descriptor for the far clipping plane.</summary>
    internal PropertyDescriptor<float> FarPlaneDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the canonical orthographic camera descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    internal static OrthographicCameraDescriptors Build()
        => new(
            orthographicSize: FloatDescriptor(
                "/orthographic_size",
                "Orthographic Size",
                static camera => camera.OrthographicSize,
                static (camera, value) => camera.OrthographicSize = value,
                Positive(SceneDiagnosticCodes.OrthographicCameraSizeNonPositive, "Orthographic size must be greater than zero.")),
            aspectMode: new(
                id: new PropertyId<CameraAspectMode>(SceneDocumentCommandService.OrthographicCameraKind, "/aspect_mode"),
                reader: static target => ((OrthographicCamera)target).AspectMode,
                writer: static (target, value) => ((OrthographicCamera)target).AspectMode = value,
                validator: static value => Enum.IsDefined(value)
                    ? ValidationResult.Ok
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Unknown camera aspect mode."),
                annotation: Annotation("aspect_mode", new EditorAnnotation { Group = "Projection", Label = "Aspect Mode", Renderer = "combobox" }),
                engineCommandKey: "orthographic_camera.aspect_mode"),
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
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers.")));

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
            id: new PropertyId<float>(SceneDocumentCommandService.OrthographicCameraKind, pointer),
            reader: target => read((OrthographicCamera)target),
            writer: (target, value) => write((OrthographicCamera)target, value),
            validator: validate,
            annotation: Annotation(pointer.TrimStart('/'), new EditorAnnotation { Group = "Projection", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: "orthographic_camera." + pointer.TrimStart('/'));

    private static EditorAnnotation Annotation(string property, EditorAnnotation fallback)
        => SceneEditorSchemaAnnotations.Get("#/definitions/orthographic_camera/" + property, fallback);
}
