// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.SceneInspector.Geometry;
using Oxygen.Editor.SceneInspector.Scripting;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Constructs inspector-local editors from the host's existing scoped dependencies.</summary>
internal static class InspectorEditorFactory
{
    /// <summary>Creates construction operations while leaving cached instance ownership with the host.</summary>
    /// <param name="hosting">The existing WinUI hosting context.</param>
    /// <param name="assets">The shared asset catalog.</param>
    /// <param name="slots">The geometry slot inventory.</param>
    /// <param name="projects">The active project lifetime.</param>
    /// <param name="logger">The scoped logging factory.</param>
    /// <param name="authoring">The authoring of the scene being inspected.</param>
    /// <param name="scripts">The scripting services, when the host provides them.</param>
    /// <returns>The component construction operations.</returns>
    internal static Dictionary<Type, Func<IPropertyEditor<SceneNode>>> Create(
        HostingContext hosting,
        IAssetFeed assets,
        IGeometryMaterialSlotProvider slots,
        IProjectContextService projects,
        ILoggerFactory? logger,
        Func<SceneAuthoring?> authoring,
        InspectorScriptServices? scripts = null)
        => new()
        {
            [typeof(TransformComponent)] = () => new TransformViewModel(logger, authoring),
            [typeof(GeometryComponent)] = () => new GeometryViewModel(hosting, assets, slots, projects, authoring),
            [typeof(PerspectiveCamera)] = () => new PerspectiveCameraViewModel(authoring),
            [typeof(OrthographicCamera)] = () => new OrthographicCameraViewModel(authoring),
            [typeof(DirectionalLightComponent)] = () => new DirectionalLightViewModel(authoring),
            [typeof(PointLightComponent)] = () => new PointLightViewModel(authoring),
            [typeof(SpotLightComponent)] = () => new SpotLightViewModel(authoring),
            [typeof(LocalFogVolumeComponent)] = () => new LocalFogVolumeViewModel(authoring),
            [typeof(ScriptingComponent)] = () => new ScriptingViewModel(authoring, assets, scripts?.Scripts, scripts?.CodeEditor, scripts?.Creation, scripts?.Problems, projects, hosting.DispatcherScheduler),
        };
}
