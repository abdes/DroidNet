// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Testably.Abstractions;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class CatalogWorkloadServices : IDisposable
{
    private readonly Oxygen.Testing.TemporaryNativeArtifacts compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();

    public CatalogWorkloadServices(NativeSceneFixture fixture, IContentPipelineProcessRunner? processRunner = null)
    {
        this.Projects = fixture.Projects;
        this.Documents = fixture.UnsavedDocuments;
        this.Projects.Activate(fixture.Projects.ActiveProject! with { AuthoringMounts = [new("Content", "Content")] });
        var files = new NativeAtomicFileStore(new RealFileSystem());
        this.Runs = new ContentCookCoordinator(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), processRunner ?? new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.compatibility);
        this.Scopes = new ProjectCookScopeProvider(this.Storage);
        this.Publication = new(this.Runs, this.Projects, files, new ProjectManagerService(this.Storage, atomicFiles: files), this.EngineContent);
        this.Mounts = new();
        this.Pipeline = new ContentPipelineService(this.Projects, this.Runs, this.Scopes, new SceneDescriptorGenerator(), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, this.compatibility, files, this.Publication, this.EngineContent);
    }

    public ProjectContextService Projects { get; }

    public NativeStorageProvider Storage { get; } = new(new RealFileSystem());

    public IUnsavedDocuments Documents { get; }

    public ContentCookCoordinator Runs { get; }

    public ProjectCookScopeProvider Scopes { get; }

    public ContentPipelineService Pipeline { get; }

    public CookPublicationService Publication { get; }

    public CookedContentMountService Mounts { get; }

    public EngineContentPak EngineContent { get; } = EngineContentPak.Installed();

    public void Dispose()
    {
        this.Runs.Dispose();
        this.compatibility.Dispose();
    }
}
