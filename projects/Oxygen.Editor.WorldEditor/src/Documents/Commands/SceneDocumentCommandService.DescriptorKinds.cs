// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Component kinds edited only through descriptors and the shared gesture pipeline: their
/// history, undo and live sync need no specialized side effects.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    /// <summary>Component kind id used by orthographic camera property identities.</summary>
    public const string OrthographicCameraKind = "orthographic-camera";

    /// <summary>Component kind id used by point light property identities.</summary>
    public const string PointLightKind = "point-light";

    /// <summary>Component kind id used by spot light property identities.</summary>
    public const string SpotLightKind = "spot-light";

    /// <summary>Property kind id used by authored node rendering flags.</summary>
    public const string NodeRenderingKind = "node-rendering";

    /// <summary>Gets the canonical descriptor catalog for orthographic cameras.</summary>
    internal static OrthographicCameraDescriptors OrthographicCamera { get; } = OrthographicCameraDescriptors.Build();

    /// <summary>Gets the canonical descriptor catalog for point lights.</summary>
    internal static LocalLightDescriptors PointLight { get; } = LocalLightDescriptors.BuildPoint();

    /// <summary>Gets the canonical descriptor catalog for spot lights.</summary>
    internal static LocalLightDescriptors SpotLight { get; } = LocalLightDescriptors.BuildSpot();

    /// <summary>Gets the canonical descriptor catalog for node rendering flags.</summary>
    internal static NodeRenderingDescriptors NodeRendering { get; } = NodeRenderingDescriptors.Build();

    private static bool IsDescriptorOnlyKind(string kind)
        => kind is OrthographicCameraKind or PointLightKind or SpotLightKind or NodeRenderingKind;

    private static IReadOnlyDictionary<PropertyId, PropertyDescriptor>? DescriptorOnlyCatalog(string kind)
        => kind switch
        {
            OrthographicCameraKind => OrthographicCamera.ById,
            PointLightKind => PointLight.ById,
            SpotLightKind => SpotLight.ById,
            NodeRenderingKind => NodeRendering.ById,
            _ => null,
        };

    private static object? DescriptorOnlyTarget(string kind, SceneNode node)
        => kind switch
        {
            OrthographicCameraKind => node.Components.OfType<OrthographicCamera>().FirstOrDefault(),
            PointLightKind => node.Components.OfType<PointLightComponent>().FirstOrDefault(),
            SpotLightKind => node.Components.OfType<SpotLightComponent>().FirstOrDefault(),
            NodeRenderingKind => node,
            _ => null,
        };

    private static string DescriptorOnlyOperationKind(string kind)
        => kind switch
        {
            OrthographicCameraKind => SceneOperationKinds.EditOrthographicCamera,
            PointLightKind => SceneOperationKinds.EditPointLight,
            SpotLightKind => SceneOperationKinds.EditSpotLight,
            _ => SceneOperationKinds.EditNodeRendering,
        };

    /// <summary>Validates the complete candidate a descriptor-only edit would produce.</summary>
    private static ValidationIssue? ValidateDescriptorOnlyCandidate(SceneNode node, string kind, PropertyEdit edit)
        => kind switch
        {
            OrthographicCameraKind => ValidateOrthographicCameraCandidate(node, edit),
            PointLightKind or SpotLightKind => ValidateLocalLightCandidate(node, kind, edit),
            _ => null,
        };

    private static ValidationIssue? ValidateOrthographicCameraCandidate(SceneNode node, PropertyEdit edit)
    {
        if (node.Components.OfType<OrthographicCamera>().FirstOrDefault() is not { } camera)
        {
            return null;
        }

        var near = edit.GetTyped(OrthographicCamera.NearPlane, out var editedNear) ? editedNear : camera.NearPlane;
        var far = edit.GetTyped(OrthographicCamera.FarPlane, out var editedFar) ? editedFar : camera.FarPlane;
        return near < far
            ? null
            : new(SceneDiagnosticCodes.OrthographicCameraNearFarInvalid, "Camera was not edited", "Near plane must be smaller than far plane.", IsFailure: true);
    }

    private static ValidationIssue? ValidateLocalLightCandidate(SceneNode node, string kind, PropertyEdit edit)
    {
        var source = node.Components.OfType<LightComponent>().FirstOrDefault();
        if (source is not (PointLightComponent or SpotLightComponent))
        {
            return null;
        }

        var candidate = (LightComponent)GameComponent.CreateAndHydrate(source.Dehydrate());
        PropertyApply.ApplyToTarget(candidate, edit, DescriptorOnlyCatalog(kind)!);
        return LightValidation.Validate((LightComponentData)candidate.Dehydrate()) is { } error
            ? new(SceneDiagnosticCodes.LocalLightInvalid, "Light was not edited", error, IsFailure: true)
            : null;
    }

    /// <summary>Converts the touched descriptor values of one node into engine entries.</summary>
    private static List<EnginePropertyValueEntry> BuildDescriptorOnlyPropertyEntries(string kind, PropertyEdit edit)
        => kind switch
        {
            OrthographicCameraKind => BuildOrthographicCameraPropertyEntries(edit),
            PointLightKind => BuildLocalLightPropertyEntries(PointLight, EngineComponentId.PointLight, edit),
            SpotLightKind => BuildLocalLightPropertyEntries(SpotLight, EngineComponentId.SpotLight, edit),
            NodeRenderingKind => BuildNodeRenderingPropertyEntries(edit),
            _ => [],
        };

    private static List<EnginePropertyValueEntry> BuildOrthographicCameraPropertyEntries(PropertyEdit edit)
    {
        var entries = new List<EnginePropertyValueEntry>(edit.Count);
        AddFloat(entries, edit, OrthographicCamera.OrthographicSize, EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.OrthographicSize);
        AddFloat(entries, edit, OrthographicCamera.AspectRatio, EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.AspectRatio);
        AddFloat(entries, edit, OrthographicCamera.NearPlane, EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.NearPlane);
        AddFloat(entries, edit, OrthographicCamera.FarPlane, EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.FarPlane);
        if (edit.GetTyped(OrthographicCamera.AspectMode, out var mode))
        {
            entries.Add(new(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.AspectMode, (float)mode));
        }

        return entries;
    }

    /// <summary>Builds the complete engine payload that projects an orthographic camera.</summary>
    private static List<EnginePropertyValueEntry> BuildOrthographicCameraPropertyEntries(Oxygen.Editor.World.OrthographicCamera camera)
        =>
        [
            new(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.OrthographicSize, camera.OrthographicSize),
            new(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.AspectRatio, camera.AspectRatio),
            new(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.NearPlane, camera.NearPlane),
            new(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.FarPlane, camera.FarPlane),
            new(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.AspectMode, (float)camera.AspectMode),
        ];

    private static List<EnginePropertyValueEntry> BuildLocalLightPropertyEntries(
        LocalLightDescriptors descriptors,
        EngineComponentId component,
        PropertyEdit edit)
    {
        var entries = new List<EnginePropertyValueEntry>(edit.Count + 2);
        if (edit.GetTyped(new PropertyId<Vector3>(descriptors.ColorDescriptor.Id), out var color))
        {
            entries.Add(new(component, (ushort)LocalLightField.ColorR, color.X));
            entries.Add(new(component, (ushort)LocalLightField.ColorG, color.Y));
            entries.Add(new(component, (ushort)LocalLightField.ColorB, color.Z));
        }

        AddBool(entries, edit, descriptors.AffectsWorldDescriptor, component, LocalLightField.AffectsWorld);
        AddBool(entries, edit, descriptors.CastsShadowsDescriptor, component, LocalLightField.CastsShadows);
        AddBool(entries, edit, descriptors.ContactShadowsDescriptor, component, LocalLightField.ContactShadows);
        AddFloat(entries, edit, new PropertyId<float>(descriptors.ExposureCompensationDescriptor.Id), component, (ushort)LocalLightField.ExposureCompensation);
        AddFloat(entries, edit, new PropertyId<float>(descriptors.ShadowBiasDescriptor.Id), component, (ushort)LocalLightField.ShadowBias);
        AddFloat(entries, edit, new PropertyId<float>(descriptors.ShadowNormalBiasDescriptor.Id), component, (ushort)LocalLightField.ShadowNormalBias);
        AddFloat(entries, edit, new PropertyId<float>(descriptors.LuminousFluxLumensDescriptor.Id), component, (ushort)LocalLightField.LuminousFluxLumens);
        AddFloat(entries, edit, new PropertyId<float>(descriptors.RangeDescriptor.Id), component, (ushort)LocalLightField.Range);
        AddFloat(entries, edit, new PropertyId<float>(descriptors.SourceRadiusDescriptor.Id), component, (ushort)LocalLightField.SourceRadius);
        if (edit.GetTyped(new PropertyId<ShadowResolutionHint>(descriptors.ShadowResolutionHintDescriptor.Id), out var hint))
        {
            entries.Add(new(component, (ushort)LocalLightField.ShadowResolutionHint, (float)hint));
        }

        if (descriptors.InnerConeAngleRadiansDescriptor is { } inner && descriptors.OuterConeAngleRadiansDescriptor is { } outer)
        {
            AddFloat(entries, edit, new PropertyId<float>(inner.Id), component, (ushort)LocalLightField.InnerConeAngleRadians);
            AddFloat(entries, edit, new PropertyId<float>(outer.Id), component, (ushort)LocalLightField.OuterConeAngleRadians);
        }

        return entries;
    }

    private static List<EnginePropertyValueEntry> BuildNodeRenderingPropertyEntries(PropertyEdit edit)
    {
        var entries = new List<EnginePropertyValueEntry>(edit.Count);
        AddBool(entries, edit, NodeRendering.IsVisibleDescriptor, EngineComponentId.Node, NodeField.Visible);
        AddBool(entries, edit, NodeRendering.CastsShadowsDescriptor, EngineComponentId.Node, NodeField.CastsShadows);
        AddBool(entries, edit, NodeRendering.ReceivesShadowsDescriptor, EngineComponentId.Node, NodeField.ReceivesShadows);
        return entries;
    }

    /// <summary>Builds the complete engine payload that projects authored node flags.</summary>
    private static List<EnginePropertyValueEntry> BuildNodeRenderingPropertyEntries(SceneNode node)
        =>
        [
            new(EngineComponentId.Node, (ushort)NodeField.Visible, node.IsVisible ? 1f : 0f),
            new(EngineComponentId.Node, (ushort)NodeField.CastsShadows, node.CastsShadows ? 1f : 0f),
            new(EngineComponentId.Node, (ushort)NodeField.ReceivesShadows, node.ReceivesShadows ? 1f : 0f),
        ];

    private static void AddFloat(List<EnginePropertyValueEntry> entries, PropertyEdit edit, PropertyId<float> id, EngineComponentId component, ushort field)
    {
        if (edit.GetTyped(id, out var value))
        {
            entries.Add(new(component, field, value));
        }
    }

    private static void AddBool<TField>(List<EnginePropertyValueEntry> entries, PropertyEdit edit, PropertyDescriptor<bool> descriptor, EngineComponentId component, TField field)
        where TField : struct, Enum
    {
        if (edit.GetTyped(new PropertyId<bool>(descriptor.Id), out var value))
        {
            entries.Add(new(component, Convert.ToUInt16(field, System.Globalization.CultureInfo.InvariantCulture), value ? 1f : 0f));
        }
    }
}
