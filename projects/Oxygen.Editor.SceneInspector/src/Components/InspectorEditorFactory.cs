// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Fog;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.Components.Instances;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.SceneInspector.Components.Scripting;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components;

/// <summary>Constructs inspector-local editors from the host's existing scoped dependencies.</summary>
internal static class InspectorEditorFactory
{
    /// <summary>Creates construction operations while leaving cached instance ownership with the host.</summary>
    /// <param name="hosting">The existing WinUI hosting context.</param>
    /// <param name="assets">The shared asset catalog.</param>
    /// <param name="surfaces">The geometry surface inventory.</param>
    /// <param name="session">The project session the inspector belongs to.</param>
    /// <param name="logger">The scoped logging factory.</param>
    /// <param name="authoring">The authoring of the scene being inspected.</param>
    /// <param name="selectNode">Selects a node of the inspected scene.</param>
    /// <param name="references">Names a selected node for its edits, a node an instance brings in by its instance root and path.</param>
    /// <param name="scripts">The scripting services, when the host provides them.</param>
    /// <param name="instances">The services the Scene Instance section swaps sources with, when the host provides them.</param>
    /// <returns>The component construction operations.</returns>
    internal static Dictionary<Type, Func<ComponentPropertyEditor>> Create(
        HostingContext hosting,
        IAssetFeed assets,
        IGeometrySurfaceProvider surfaces,
        IProjectSession session,
        ILoggerFactory? logger,
        Func<SceneAuthoring?> authoring,
        Action<NodeId> selectNode,
        Func<SceneNode, NodeReference> references,
        InspectorScriptServices? scripts = null,
        InspectorInstanceServices? instances = null)
        => new()
        {
            [typeof(TransformComponent)] = () => new TransformViewModel(logger, authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(NodeFlagsComponent)] = () => new NodeRenderingViewModel(authoring, selectNode) { References = references, SourceApply = instances?.SourceApply },
            [typeof(GeometryComponent)] = () => new GeometryViewModel(hosting, assets, surfaces, session, authoring, logger) { References = references, SourceApply = instances?.SourceApply },
            [typeof(PerspectiveCamera)] = () => new PerspectiveCameraViewModel(authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(OrthographicCamera)] = () => new OrthographicCameraViewModel(authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(DirectionalLightComponent)] = () => new DirectionalLightViewModel(authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(PointLightComponent)] = () => new PointLightViewModel(authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(SpotLightComponent)] = () => new SpotLightViewModel(authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(LocalFogVolumeComponent)] = () => new LocalFogVolumeViewModel(authoring) { References = references, SourceApply = instances?.SourceApply },
            [typeof(SceneInstanceComponent)] = () => new SceneInstanceViewModel(authoring, session, hosting.DispatcherScheduler, instances ?? new InspectorInstanceServices(assets), logger) { References = references, SourceApply = instances?.SourceApply },
            [typeof(ScriptingComponent)] = () => new ScriptingViewModel(authoring, assets, scripts?.Scripts, scripts?.CodeEditor, scripts?.Creation, scripts?.Problems, session, hosting.DispatcherScheduler),
        };
}
