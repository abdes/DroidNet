// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for perspective camera inspector properties.
/// </summary>
internal sealed class PerspectiveCameraDescriptors
{
    private PerspectiveCameraDescriptors(
        PropertyDescriptor<float> fieldOfViewDegrees,
        PropertyDescriptor<float> aspectRatio,
        PropertyDescriptor<float> nearPlane,
        PropertyDescriptor<float> farPlane)
    {
        this.FieldOfViewDegrees = new PropertyId<float>(fieldOfViewDegrees.Id);
        this.AspectRatio = new PropertyId<float>(aspectRatio.Id);
        this.NearPlane = new PropertyId<float>(nearPlane.Id);
        this.FarPlane = new PropertyId<float>(farPlane.Id);
        this.FieldOfViewDegreesDescriptor = fieldOfViewDegrees;
        this.AspectRatioDescriptor = aspectRatio;
        this.NearPlaneDescriptor = nearPlane;
        this.FarPlaneDescriptor = farPlane;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [fieldOfViewDegrees.Id] = fieldOfViewDegrees,
            [aspectRatio.Id] = aspectRatio,
            [nearPlane.Id] = nearPlane,
            [farPlane.Id] = farPlane,
        };
    }

    /// <summary>Gets the typed property id for field of view.</summary>
    internal PropertyId<float> FieldOfViewDegrees { get; }

    /// <summary>Gets the typed property id for aspect ratio.</summary>
    internal PropertyId<float> AspectRatio { get; }

    /// <summary>Gets the typed property id for the near clipping plane.</summary>
    internal PropertyId<float> NearPlane { get; }

    /// <summary>Gets the typed property id for the far clipping plane.</summary>
    internal PropertyId<float> FarPlane { get; }

    /// <summary>Gets the descriptor for field of view.</summary>
    internal PropertyDescriptor<float> FieldOfViewDegreesDescriptor { get; }

    /// <summary>Gets the descriptor for aspect ratio.</summary>
    internal PropertyDescriptor<float> AspectRatioDescriptor { get; }

    /// <summary>Gets the descriptor for the near clipping plane.</summary>
    internal PropertyDescriptor<float> NearPlaneDescriptor { get; }

    /// <summary>Gets the descriptor for the far clipping plane.</summary>
    internal PropertyDescriptor<float> FarPlaneDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical perspective camera descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    internal static PerspectiveCameraDescriptors Build()
        => new(
            fieldOfViewDegrees: FloatDescriptor(
                "/field_of_view_degrees",
                "Field of View",
                static camera => camera.FieldOfView,
                static (camera, value) => camera.FieldOfView = Math.Clamp(value, 1f, 179f),
                static value => float.IsFinite(value)
                    ? ValidationResult.Ok
                    : ValidationResult.Fail(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera values must be finite numbers."),
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
                "perspective_camera.far_plane"));

    private static PropertyDescriptor<float> FloatDescriptor(
        string pointer,
        string label,
        Func<PerspectiveCamera, float> read,
        Action<PerspectiveCamera, float> write,
        Func<float, ValidationResult> validate,
        string engineCommandKey)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.PerspectiveCameraKind, pointer),
            reader: target => read((PerspectiveCamera)target),
            writer: (target, value) => write((PerspectiveCamera)target, value),
            validator: validate,
            annotation: SceneEditorSchemaAnnotations.Get(
                ToSceneSchemaPointer(engineCommandKey),
                new EditorAnnotation { Group = "Projection", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: engineCommandKey);

    private static string ToSceneSchemaPointer(string engineCommandKey)
        => engineCommandKey switch
        {
            "perspective_camera.field_of_view_degrees" => "#/definitions/perspective_camera/fov_y",
            "perspective_camera.aspect_ratio" => "#/definitions/perspective_camera/aspect_ratio",
            "perspective_camera.near_plane" => "#/definitions/perspective_camera/near_plane",
            "perspective_camera.far_plane" => "#/definitions/perspective_camera/far_plane",
            _ => throw new ArgumentOutOfRangeException(nameof(engineCommandKey), engineCommandKey, "Unknown perspective camera command key."),
        };
}
