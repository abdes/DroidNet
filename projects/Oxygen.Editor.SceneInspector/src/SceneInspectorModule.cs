// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.SceneInspector.Geometry;
using Oxygen.Editor.SceneInspector.Scripting;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.SceneInspector;

/// <summary>The Properties panel of a scene: the selected nodes' components, the scene's environment and the script slots.</summary>
public sealed class SceneInspectorModule : IFeatureModule
{
    /// <summary>The Properties panel's identity.</summary>
    public const string PanelId = "props";

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Properties", typeof(SceneNodeEditorViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        window.Register<SceneNodeEditorView>(Reuse.Transient);

        // The component and environment editors, whose views the inspector's view locator resolves.
        window.Register<TransformViewModel>(Reuse.Transient);
        window.Register<TransformView>(Reuse.Transient);
        window.Register<PerspectiveCameraViewModel>(Reuse.Transient);
        window.Register<PerspectiveCameraView>(Reuse.Transient);
        window.Register<DirectionalLightViewModel>(Reuse.Transient);
        window.Register<DirectionalLightView>(Reuse.Transient);
        window.Register<OrthographicCameraViewModel>(Reuse.Transient);
        window.Register<OrthographicCameraView>(Reuse.Transient);
        window.Register<PointLightViewModel>(Reuse.Transient);
        window.Register<PointLightView>(Reuse.Transient);
        window.Register<SpotLightViewModel>(Reuse.Transient);
        window.Register<SpotLightView>(Reuse.Transient);
        window.Register<LocalFogVolumeViewModel>(Reuse.Transient);
        window.Register<LocalFogVolumeView>(Reuse.Transient);
        window.Register<ScriptingViewModel>(Reuse.Transient);
        window.Register<ScriptingView>(Reuse.Transient);
        window.Register<NodeRenderingViewModel>(Reuse.Transient);
        window.Register<NodeRenderingView>(Reuse.Transient);
        window.Register<EnvironmentViewModel>(Reuse.Transient);
        window.Register<EnvironmentView>(Reuse.Transient);
        window.Register<GeometryViewModel>(Reuse.Transient);
        window.Register<GeometryView>(Reuse.Transient);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window) => ArgumentNullException.ThrowIfNull(window);
}
