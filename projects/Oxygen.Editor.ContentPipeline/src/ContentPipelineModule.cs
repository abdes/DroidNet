// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
///     The content pipeline in the editor scope: the SDK's engine content, import manifests, the native import tool,
///     the cook and its publication, cooked-content mounts, material cooks and the session's asset relocations.
/// </summary>
/// <remarks>
///     The project session owns these services once it exists; until then they are the editor's, and each workspace
///     window adds the services of <see cref="ContentPipelineWindowModule" />.
/// </remarks>
public sealed class ContentPipelineModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // The SDK's engine content pak: the /Engine assets every project references, read from the installation.
        scope.RegisterDelegate<IEngineContentPak>(
            _ => EngineContentPak.Installed(),
            Reuse.Singleton);
        scope.Register<ISceneDescriptorGenerator, SceneDescriptorGenerator>(Reuse.Singleton);
        scope.Register<IContentImportManifestBuilder, ContentImportManifestBuilder>(Reuse.Singleton);
        scope.Register<IContentImportManifestValidator, ContentImportManifestValidator>(Reuse.Singleton);
        RegisterImportTool(scope);
        RegisterCook(scope);

        // The session's asset relocations, resolved by undo, redo and paste in every open document.
        scope.Register<AssetRedirects>(Reuse.Singleton);
        scope.RegisterMapping<IAssetRedirects, AssetRedirects>();
    }

    // The native import tool, which runs with the cooking compatibility of the editor's native SDK.
    private static void RegisterImportTool(IRegistrator scope)
    {
        scope.Register<IEngineContentPipelineToolLocator, EngineContentPipelineToolLocator>(Reuse.Singleton);
        scope.Register<IContentPipelineProcessRunner, ContentPipelineProcessRunner>(Reuse.Singleton);
        scope.RegisterDelegate<INativeCompatibilityService>(
            _ => EditorNativeCompatibilityService.ForCooking(),
            Reuse.Singleton,
            serviceKey: EditorNativeCompatibilityService.CookingServiceKey);
        scope.RegisterDelegate<ImportToolContentPipelineApi>(
            resolver => new(
            resolver.Resolve<IEngineContentPipelineToolLocator>(),
            resolver.Resolve<IContentPipelineProcessRunner>(),
            resolver.Resolve<ILogger<ImportToolContentPipelineApi>>(),
            resolver.Resolve<INativeCompatibilityService>(EditorNativeCompatibilityService.CookingServiceKey)),
            Reuse.Singleton);
        scope.RegisterMapping<IEngineContentPipelineApi, ImportToolContentPipelineApi>();
    }

    // The cook, the pipeline service over it and the ports the service implements, the cook's publication and the
    // mounts of cooked content.
    private static void RegisterCook(IRegistrator scope)
    {
        scope.Register<ContentCookCoordinator>(Reuse.Singleton);
        scope.RegisterMapping<IContentCookCoordinator, ContentCookCoordinator>();
        scope.RegisterMapping<ICookRunService, ContentCookCoordinator>();
        scope.RegisterDelegate<IContentPipelineService>(
            resolver => new ContentPipelineService(
            resolver.Resolve<IProjectContextService>(),
            resolver.Resolve<IContentCookCoordinator>(),
            resolver.Resolve<IProjectCookScopeProvider>(),
            resolver.Resolve<ISceneDescriptorGenerator>(),
            resolver.Resolve<IContentImportManifestBuilder>(),
            resolver.Resolve<IContentImportManifestValidator>(),
            resolver.Resolve<IEngineContentPipelineApi>(),
            resolver.Resolve<INativeCompatibilityService>(EditorNativeCompatibilityService.CookingServiceKey),
            resolver.Resolve<IAtomicFileStore>(),
            resolver.Resolve<CookPublicationService>(),
            resolver.Resolve<IEngineContentPak>()),
            Reuse.Singleton);
        scope.RegisterDelegate(
            resolver => (IGeometryMaterialSlotProvider)resolver.Resolve<IContentPipelineService>(), Reuse.Singleton);
        scope.RegisterDelegate(
            resolver => (IScriptLibrary)resolver.Resolve<IContentPipelineService>(), Reuse.Singleton);
        scope.Register<CookPublicationService>(Reuse.Singleton);
        scope.RegisterMapping<ICookPublicationReader, CookPublicationService>();
        scope.Register<CookedContentMountService>(Reuse.Singleton);
        scope.Register<IMaterialCookService, MaterialCookService>(Reuse.Singleton);
    }
}
