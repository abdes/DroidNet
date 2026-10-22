// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Storage;
using DryIoc;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
///     The content pipeline in a project session's scope: the project's cook, the pipeline service over it and the ports
///     the service implements, the cook's publication, the mounts of cooked content, material cooks, the unsaved
///     documents a cook reads, the automatic cook, the cook status of the project's assets, their relocation and
///     creation, the session's relocation record, and the project's files and relocations as documents see them.
/// </summary>
/// <remarks>
///     Each service is created for the session's project, and closing the session cancels its work and disposes it.
/// </remarks>
public sealed class ContentPipelineSessionModule : IScopeModule<ISessionLayer>
{
    // The cook listens to the catalog's changes from the moment the session opens.
    private static readonly Type[] Activated = [typeof(IAutomaticCookService)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        RegisterCook(scope);

        RegisterAssets(scope);

        // The project's files and relocations, as documents see them, in the session's current content mounts.
        scope.Register<IDocumentFiles, ProjectDocumentFiles>(Reuse.Singleton);
        scope.Register<IDocumentRelocations, DocumentRelocationFeed>(Reuse.Singleton);
    }

    // The cook status of the project's assets, their relocation and creation, and the session's relocation record,
    // resolved by undo, redo and paste in every open document.
    private static void RegisterAssets(IRegistrator scope)
    {
        scope.RegisterDelegate<IAssetCookStatusReader>(
            resolver => resolver.Resolve<IContentPipelineService>(), Reuse.Singleton);
        scope.Register<IAssetRelocationService, AssetRelocationService>(Reuse.Singleton);
        scope.Register<IAssetAuthoring, AssetAuthoring>(Reuse.Singleton);
        scope.Register<AssetRedirects>(Reuse.Singleton);
        scope.RegisterMapping<IAssetRedirects, AssetRedirects>();
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
            resolver.Resolve<IProjectSession>(),
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
            resolver => (IGeometrySurfaceProvider)resolver.Resolve<IContentPipelineService>(), Reuse.Singleton);
        scope.RegisterDelegate(
            resolver => (IScriptLibrary)resolver.Resolve<IContentPipelineService>(), Reuse.Singleton);
        scope.Register<CookPublicationService>(Reuse.Singleton);
        scope.RegisterMapping<ICookPublicationReader, CookPublicationService>();
        scope.Register<CookedContentMountService>(Reuse.Singleton);
        scope.Register<IMaterialCookService, MaterialCookService>(Reuse.Singleton);
        scope.Register<IUnsavedCookInputs, UnsavedCookInputs>(Reuse.Singleton);
        scope.Register<IAutomaticCookService, AutomaticCookService>(Reuse.Singleton);
    }
}
