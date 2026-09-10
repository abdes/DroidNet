// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;
using Oxygen.Interop.Input;
using Oxygen.Interop.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Owns the concrete world/input facades and all native payload construction.</summary>
internal sealed class NativeRuntimeCommandTransport(EngineContext context) : IRuntimeCommandTransport
{
    private readonly OxygenWorld world = new(context);
    private readonly OxygenInput input = new(context);

    /// <inheritdoc/>
    public void Execute(RuntimeWorldCommand command)
    {
        switch (command)
        {
            case RuntimeRemoveSceneNode value:
                this.world.RemoveSceneNode(value.NodeId);
                break;
            case RuntimeRenameSceneNode value:
                this.world.RenameSceneNode(value.NodeId, value.NewName);
                break;
            case RuntimeSetLocalTransform value:
                this.world.SetLocalTransform(value.NodeId, value.Position, value.Rotation, value.Scale);
                break;
            case RuntimeSetProperties value:
                this.world.SetProperties(value.NodeId, RuntimeTransportConversion.ToNative(value.Entries));
                break;
            case RuntimeSetGeometry value:
                this.world.SetGeometry(value.NodeId, value.AssetPath);
                break;
            case RuntimeSetMaterialOverride value:
                this.world.SetMaterialOverride(value.NodeId, value.SlotIndex, value.MaterialPath);
                break;
            case RuntimeSetEnvironment value:
                this.world.SetEnvironment(value.AtmosphereEnabled, value.SunDiskEnabled, value.PlanetRadiusMeters, value.AtmosphereHeightMeters, value.GroundAlbedoRgb, value.RayleighScaleHeightMeters, value.MieScaleHeightMeters, value.MieAnisotropy, value.SkyLuminanceFactorRgb, value.AerialPerspectiveDistanceScale, value.AerialScatteringStrength, value.AerialPerspectiveStartDepthMeters, value.HeightFogContribution, value.ExposureMode, value.ExposureEnabled, value.ExposureKey, value.ManualExposureEv, value.ExposureCompensation, value.ToneMapping, value.AutoExposureMeteringMode, value.AutoExposureMinEv, value.AutoExposureMaxEv, value.AutoExposureSpeedUp, value.AutoExposureSpeedDown, value.AutoExposureLowPercentile, value.AutoExposureHighPercentile, value.AutoExposureMinLogLuminance, value.AutoExposureLogLuminanceRange, value.AutoExposureTargetLuminance, value.AutoExposureSpotMeterRadius, value.BloomIntensity, value.BloomThreshold, value.Saturation, value.Contrast, value.VignetteIntensity, value.DisplayGamma);
                break;
            case RuntimeDetachGeometry value:
                this.world.DetachGeometry(value.NodeId);
                break;
            case RuntimeSelectNode value:
                this.world.SelectNode(value.NodeId);
                break;
            case RuntimeDeselectNode value:
                this.world.DeselectNode(value.NodeId);
                break;
            case RuntimeReparentSceneNode value:
                this.world.ReparentSceneNode(value.Child, value.Parent, value.PreserveWorldTransform);
                break;
            case RuntimeReparentSceneNodes value:
                this.world.ReparentSceneNodes(value.Children.ToArray(), value.Parent, value.PreserveWorldTransform);
                break;
            case RuntimeUpdateTransformsForNodes value:
                this.world.UpdateTransformsForNodes(value.Nodes.ToArray());
                break;
            case RuntimeRemoveSceneNodes value:
                this.world.RemoveSceneNodes(value.Nodes.ToArray());
                break;
            default:
                this.ExecuteComponent(command);
                break;
        }
    }

    /// <inheritdoc/>
    public Task<bool> ActivateSceneAsync(string name)
    {
        this.world.DestroyScene();
        return this.world.CreateSceneAsync(name);
    }

    /// <inheritdoc/>
    public Task CreateNodeAsync(RuntimeCreateNode command)
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        this.world.CreateSceneNode(command.Name, command.NodeId, command.ParentId, _ => completion.TrySetResult(), command.InitializeWorldAsRoot);
        return completion.Task;
    }

    /// <inheritdoc/>
    public void ExecuteInput(ulong viewId, RuntimeInputEvent inputEvent)
    {
        var target = new ViewIdManaged(viewId);
        switch (inputEvent)
        {
            case RuntimeKeyEvent value:
                this.input.PushKeyEvent(target, RuntimeTransportConversion.ToNative(value));
                break;
            case RuntimeButtonEvent value:
                this.input.PushButtonEvent(target, RuntimeTransportConversion.ToNative(value));
                break;
            case RuntimeMouseMotionEvent value:
                this.input.PushMouseMotion(target, RuntimeTransportConversion.ToNative(value));
                break;
            case RuntimeMouseWheelEvent value:
                this.input.PushMouseWheel(target, RuntimeTransportConversion.ToNative(value));
                break;
            case RuntimeFocusLostEvent:
                this.input.OnFocusLost(target);
                break;
            default:
                throw new ArgumentException("Unsupported runtime input event.", nameof(inputEvent));
        }
    }

    /// <inheritdoc/>
    public void MountCookedRoot(string path) => this.world.AddLooseCookedRoot(path);

    /// <inheritdoc/>
    public void ClearCookedRoots() => this.world.ClearCookedRoots();

    private void ExecuteComponent(RuntimeWorldCommand command)
    {
        switch (command)
        {
            case RuntimeAttachPerspectiveCamera value:
                this.world.AttachPerspectiveCamera(value.NodeId, value.FieldOfViewYRadians, value.AspectRatio, value.NearPlane, value.FarPlane);
                break;
            case RuntimeDetachCamera value:
                this.world.DetachCamera(value.NodeId);
                break;
            case RuntimeSetVisibility value:
                this.world.SetVisibility(value.NodeId, value.Visible);
                break;
            case RuntimeAttachDirectionalLight value:
                this.world.AttachDirectionalLight(value.NodeId, value.IntensityLux, value.AngularSizeRadians, value.Color, value.AffectsWorld, value.Mobility, value.CastsShadows, value.ShadowBias, value.ShadowNormalBias, value.ContactShadows, value.ShadowResolutionHint, value.ExposureCompensation, value.EnvironmentContribution, value.IsSunLight, value.CascadeCount, value.SplitMode, value.MaxShadowDistance, value.CascadeDistances, value.DistributionExponent, value.TransitionFraction, value.DistanceFadeoutFraction);
                break;
            case RuntimeAttachPointLight value:
                this.world.AttachPointLight(value.NodeId, value.LuminousFluxLumens, value.Range, value.SourceRadius, value.DecayExponent, value.Color, value.AffectsWorld, value.CastsShadows, value.ExposureCompensation);
                break;
            case RuntimeAttachSpotLight value:
                this.world.AttachSpotLight(value.NodeId, value.LuminousFluxLumens, value.Range, value.SourceRadius, value.DecayExponent, value.InnerConeAngleRadians, value.OuterConeAngleRadians, value.Color, value.AffectsWorld, value.CastsShadows, value.ExposureCompensation);
                break;
            case RuntimeDetachLight value:
                this.world.DetachLight(value.NodeId);
                break;
            default:
                throw new ArgumentException("Unsupported runtime world command.", nameof(command));
        }
    }
}
