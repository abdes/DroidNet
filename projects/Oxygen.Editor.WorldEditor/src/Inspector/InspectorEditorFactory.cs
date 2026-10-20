// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector.Scripting;

namespace Oxygen.Editor.World.Inspector;

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
    internal static Dictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> Create(
        HostingContext hosting,
        IAssetFeed assets,
        IGeometryMaterialSlotProvider slots,
        IProjectContextService projects,
        ILoggerFactory? logger,
        Func<SceneAuthoring?> authoring,
        InspectorScriptServices? scripts = null)
        => new()
        {
            [typeof(TransformComponent)] = _ => new TransformViewModel(logger, authoring),
            [typeof(GeometryComponent)] = _ => new GeometryViewModel(hosting, assets, slots, projects, authoring),
            [typeof(PerspectiveCamera)] = _ => new PerspectiveCameraViewModel(authoring),
            [typeof(OrthographicCamera)] = _ => new OrthographicCameraViewModel(authoring),
            [typeof(DirectionalLightComponent)] = _ => new DirectionalLightViewModel(authoring),
            [typeof(PointLightComponent)] = _ => new PointLightViewModel(authoring),
            [typeof(SpotLightComponent)] = _ => new SpotLightViewModel(authoring),
            [typeof(LocalFogVolumeComponent)] = _ => new LocalFogVolumeViewModel(authoring),
            [typeof(ScriptingComponent)] = messenger => new ScriptingViewModel(authoring, assets, scripts?.Scripts, scripts?.CodeEditor, messenger, projects, hosting.DispatcherScheduler),
        };
}
