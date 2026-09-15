// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Owns production cooking and catalog services for a rendered qualification project.</summary>
public sealed partial class InspectorControlTests
{
    private sealed partial class CatalogWorkloadServices : IDisposable
    {
        private readonly Oxygen.Testing.TemporaryNativeArtifacts compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();

        public CatalogWorkloadServices(NativeSceneFixture fixture, IContentPipelineProcessRunner? processRunner = null)
        {
            this.Projects.Activate(ProjectContext.FromProject(fixture.Source.Project) with { AuthoringMounts = [new("Content", "Content")] });
            var files = new NativeAtomicFileStore(new RealFileSystem());
            this.Runs = new ContentCookCoordinator(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), processRunner ?? new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.compatibility);
            this.Scopes = new ProjectCookScopeProvider(this.Storage);
            this.Publication = new(this.Runs, this.Projects, files);
            this.Mounts = new(this.Storage, api);
            this.Pipeline = new ContentPipelineService(this.Projects, this.Runs, this.Scopes, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, this.Documents, this.compatibility, files, this.Publication);
            var paths = new Mock<IPathFinder>();
            _ = paths.SetupGet(value => value.LocalAppState).Returns(Path.Combine(fixture.ProjectRoot, ".state"));
            _ = paths.SetupGet(value => value.Temp).Returns(Path.Combine(fixture.ProjectRoot, ".temp"));
            this.Builtins = new BuiltinCatalogDiscovery(api, files, paths.Object, NullLogger<BuiltinCatalogDiscovery>.Instance, this.compatibility);
        }

        public ProjectContextService Projects { get; } = new();

        public NativeStorageProvider Storage { get; } = new(new RealFileSystem());

        public CookDocumentRegistry Documents { get; } = new();

        public ContentCookCoordinator Runs { get; }

        public ProjectCookScopeProvider Scopes { get; }

        public ContentPipelineService Pipeline { get; }

        public CookPublicationService Publication { get; }

        public CookedContentMountService Mounts { get; }

        public BuiltinCatalogDiscovery Builtins { get; }

        public void Dispose()
        {
            this.Builtins.Dispose();
            this.Runs.Dispose();
            this.compatibility.Dispose();
        }
    }
}
