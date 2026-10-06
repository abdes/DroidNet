// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Constructs inspector-local editors from the host's existing scoped dependencies.</summary>
internal static class InspectorEditorFactory
{
    /// <summary>Creates construction operations while leaving cached instance ownership with the host.</summary>
    /// <param name="hosting">The existing WinUI hosting context.</param>
    /// <param name="assets">The shared asset catalog.</param>
    /// <param name="materials">The shared material picker.</param>
    /// <param name="builtins">The native built-in catalog.</param>
    /// <param name="demand">The saved-asset demand owner.</param>
    /// <param name="slots">The geometry slot inventory.</param>
    /// <param name="projects">The active project lifetime.</param>
    /// <param name="logger">The scoped logging factory.</param>
    /// <param name="commands">The existing authoring service.</param>
    /// <param name="context">The current document context provider.</param>
    /// <returns>The existing four component construction operations.</returns>
    internal static Dictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> Create(
        HostingContext hosting,
        IContentBrowserAssetProvider assets,
        IMaterialPickerService materials,
        IBuiltinCatalogDiscovery builtins,
        ISceneContentDemandService demand,
        IGeometryMaterialSlotProvider slots,
        IProjectContextService projects,
        ILoggerFactory? logger,
        ISceneDocumentCommandService commands,
        Func<SceneDocumentCommandContext?> context)
        => new()
        {
            [typeof(TransformComponent)] = _ => new TransformViewModel(logger, commands, context),
            [typeof(GeometryComponent)] = _ => new GeometryViewModel(hosting, assets, materials, builtins, demand, slots, projects, commands, context),
            [typeof(PerspectiveCamera)] = _ => new PerspectiveCameraViewModel(commands, context),
            [typeof(DirectionalLightComponent)] = _ => new DirectionalLightViewModel(commands, context),
        };
}
