// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Fog;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.Components.Instances;
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
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Properties", typeof(SceneInspectorViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // One view model per window, which its views share and the window disposes.
        scope.Register<SceneInspectorViewModel>(Reuse.Singleton);
        scope.Register<SceneInspectorView>(Reuse.Transient);

        // The views the panel's view locator resolves for its editors; the panel creates the editors themselves.
        scope.Register<TransformView>(Reuse.Transient);
        scope.Register<PerspectiveCameraView>(Reuse.Transient);
        scope.Register<DirectionalLightView>(Reuse.Transient);
        scope.Register<OrthographicCameraView>(Reuse.Transient);
        scope.Register<PointLightView>(Reuse.Transient);
        scope.Register<SpotLightView>(Reuse.Transient);
        scope.Register<LocalFogVolumeView>(Reuse.Transient);
        scope.Register<ScriptingView>(Reuse.Transient);
        scope.Register<NodeRenderingView>(Reuse.Transient);
        scope.Register<SceneSettingsView>(Reuse.Transient);
        scope.Register<GeometryView>(Reuse.Transient);
        scope.Register<SceneInstanceView>(Reuse.Transient);
    }
}
