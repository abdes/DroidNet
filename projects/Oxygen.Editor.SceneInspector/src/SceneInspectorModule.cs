// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Fog;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.SceneInspector.Components.Scripting;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.SceneSettings;
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

        // The views the panel's view locator resolves for its editors; the panel creates the editors themselves.
        window.Register<TransformView>(Reuse.Transient);
        window.Register<PerspectiveCameraView>(Reuse.Transient);
        window.Register<DirectionalLightView>(Reuse.Transient);
        window.Register<OrthographicCameraView>(Reuse.Transient);
        window.Register<PointLightView>(Reuse.Transient);
        window.Register<SpotLightView>(Reuse.Transient);
        window.Register<LocalFogVolumeView>(Reuse.Transient);
        window.Register<ScriptingView>(Reuse.Transient);
        window.Register<NodeRenderingView>(Reuse.Transient);
        window.Register<EnvironmentView>(Reuse.Transient);
        window.Register<GeometryView>(Reuse.Transient);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window) => ArgumentNullException.ThrowIfNull(window);
}
