// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Testably.Abstractions;

namespace Oxygen.Testing;

/// <summary>Composes the production native pipeline under a fixture-owned qualification manifest.</summary>
internal sealed partial class NativeContentPipelineFixture : IDisposable
{
    private readonly TemporaryArtifactQualification qualification = TemporaryArtifactQualification.ForInstalledEngine();

    /// <summary>Initializes a new instance of the <see cref="NativeContentPipelineFixture"/> class.</summary>
    /// <param name="context">The active fixture project.</param>
    /// <param name="coordinator">Its shared cook writer.</param>
    /// <param name="documents">The document owners participating in capture.</param>
    public NativeContentPipelineFixture(IProjectContextService context, IContentCookCoordinator coordinator, ICookDocumentRegistry documents)
    {
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.qualification);
        this.Pipeline = new ContentPipelineService(
            context,
            coordinator,
            new ProjectCookScopeProvider(new NativeStorageProvider(new RealFileSystem())),
            new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)),
            new ContentImportManifestBuilder(),
            new ContentImportManifestValidator(),
            api,
            documents,
            this.qualification);
    }

    /// <summary>Gets the production pipeline for fixture operations.</summary>
    public ContentPipelineService Pipeline { get; }

    /// <inheritdoc />
    public void Dispose() => this.qualification.Dispose();
}
