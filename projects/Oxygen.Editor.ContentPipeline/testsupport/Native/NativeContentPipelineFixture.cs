// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;
using Testably.Abstractions;

namespace Oxygen.Testing;

/// <summary>Composes the production native pipeline and cooking compatibility checks.</summary>
internal sealed partial class NativeContentPipelineFixture : IDisposable
{
    private readonly EditorNativeCompatibilityService compatibility = EditorNativeCompatibilityService.ForCooking();

    /// <summary>Initializes a new instance of the <see cref="NativeContentPipelineFixture"/> class.</summary>
    /// <param name="context">The active fixture project.</param>
    /// <param name="coordinator">Its shared cook writer.</param>
    /// <param name="documents">The document owners participating in capture.</param>
    public NativeContentPipelineFixture(IProjectContextService context, IContentCookCoordinator coordinator, ICookDocumentRegistry documents)
    {
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.compatibility);
        var storage = new NativeStorageProvider(new RealFileSystem());
        var files = new NativeAtomicFileStore(new RealFileSystem());
        this.Publication = new Oxygen.Editor.ContentPipeline.Publication.CookPublicationService(coordinator, context, files,
            new ProjectManagerService(storage, atomicFiles: files));
        this.Pipeline = new ContentPipelineService(
            context,
            coordinator,
            new ProjectCookScopeProvider(storage),
            new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)),
            new ContentImportManifestBuilder(),
            new ContentImportManifestValidator(),
            api,
            documents,
            this.compatibility,
            files,
            this.Publication);
    }

    /// <summary>Gets the production pipeline for fixture operations.</summary>
    public ContentPipelineService Pipeline { get; }

    /// <summary>Gets the same publication owner used by fixture cooking and runtime admission.</summary>
    public Oxygen.Editor.ContentPipeline.Publication.CookPublicationService Publication { get; }

    /// <inheritdoc />
    public void Dispose() => this.compatibility.Dispose();
}
