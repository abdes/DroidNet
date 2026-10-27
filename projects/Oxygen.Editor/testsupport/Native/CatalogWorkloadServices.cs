// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.TestSupport;

internal sealed partial class CatalogWorkloadServices : IDisposable
{
    private readonly Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts compatibility = Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts.ForInstalledEngine();

    public CatalogWorkloadServices(NativeSceneFixture fixture, IContentPipelineProcessRunner? processRunner = null)
    {
        this.Sessions = fixture.Sessions;
        this.Documents = fixture.UnsavedDocuments;
        this.Sessions.ChangeContext(fixture.Session.Context with { AuthoringMounts = [new("Content", "Content")] });
        var session = this.Sessions.Session!;
        var files = new NativeAtomicFileStore(new RealFileSystem());
        this.Runs = new ContentCookCoordinator(session, NullLogger<ContentCookCoordinator>.Instance);
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), processRunner ?? new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.compatibility);
        this.Scopes = new ProjectCookScopeProvider(this.Storage);
        this.Publication = new(this.Runs, session, files, new ProjectManagerService(this.Storage, atomicFiles: files), this.EngineContent);
        this.Pipeline = new ContentPipelineService(session, this.Runs, this.Scopes, new SceneDescriptorGenerator(), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, this.compatibility, files, this.Publication, this.EngineContent);
    }

    /// <summary>Gets the session host of the fixture's project, whose content mounts a scenario changes.</summary>
    public TestSessionHost Sessions { get; }

    /// <summary>Gets the session of the fixture's project, which the catalog, the feed and the relocations serve.</summary>
    public TestSession Session => this.Sessions.Session!;

    public NativeStorageProvider Storage { get; } = new(new RealFileSystem());

    public IUnsavedDocuments Documents { get; }

    public ContentCookCoordinator Runs { get; }

    public ProjectCookScopeProvider Scopes { get; }

    public ContentPipelineService Pipeline { get; }

    public CookPublicationService Publication { get; }

    public EngineContentPak EngineContent { get; } = EngineContentPak.Installed();

    public void Dispose()
    {
        this.Runs.Dispose();
        this.compatibility.Dispose();
    }
}
