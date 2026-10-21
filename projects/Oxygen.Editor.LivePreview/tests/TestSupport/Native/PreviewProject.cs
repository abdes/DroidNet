// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport.Native;

/// <summary>
/// A project saved in a temporary folder, with the production cook pipeline and its publication: a test writes a
/// material, cooks it, and starts the project's preview session on the shared engine.
/// </summary>
internal sealed partial class PreviewProject : IDisposable
{
    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-LivePreview-");
    private readonly ContentCookCoordinator coordinator;
    private readonly Oxygen.Testing.NativeContentPipelineFixture native;
    private readonly ProjectManagerService manager;

    /// <summary>Initializes a new instance of the <see cref="PreviewProject"/> class.</summary>
    public PreviewProject()
    {
        this.Context = TestProject.Create(new RealFileSystem(), this.directory.FullName).Context;
        _ = TestProject.SaveProjectFile(this.Context);
        this.Projects.Activate(this.Context);
        this.coordinator = new ContentCookCoordinator(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
        this.native = new Oxygen.Testing.NativeContentPipelineFixture(this.Projects, this.coordinator);
        this.manager = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: new NativeAtomicFileStore(new RealFileSystem()));
        _ = this.Catalog.Setup(value => value.UsePublicationAsync(It.IsAny<ICookPublicationReadLease>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
    }

    /// <summary>Gets the project's context, as the workspace activated it.</summary>
    public ProjectContext Context { get; }

    /// <summary>Gets the active project.</summary>
    public ProjectContextService Projects { get; } = new();

    /// <summary>Gets the asset catalog the preview hands each publication to.</summary>
    public Mock<IAssetPublicationTarget> Catalog { get; } = new();

    /// <summary>Gets the production cook pipeline.</summary>
    public ContentPipelineService Pipeline => this.native.Pipeline;

    /// <summary>Gets the project's cook publication.</summary>
    public CookPublicationService Publication => this.native.Publication;

    /// <summary>Gets the results the sessions published.</summary>
    public List<OperationResult> Results { get; } = [];

    /// <summary>Writes a material source of the project.</summary>
    /// <param name="name">The material's name.</param>
    /// <param name="baseColor">Its base color.</param>
    /// <returns>The material's asset URI.</returns>
    public AssetPath WriteMaterial(string name, Vector4 baseColor)
    {
        var relative = $"Content/Materials/{name}.omat.json";
        var path = Path.Combine(this.directory.FullName, relative.Replace('/', Path.DirectorySeparatorChar));
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var factor = JsonSerializer.Serialize(new[] { baseColor.X, baseColor.Y, baseColor.Z, baseColor.W });
        File.WriteAllText(path, $$"""{ "name": "{{name}}", "parameters": { "base_color": {{factor}}, "metalness": 0, "roughness": 0.5 } }""");
        return AssetPath.Parse($"/Content/Materials/{name}.omat");
    }

    /// <summary>Reads where the selected publication keeps the project's cooked Content.</summary>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The cooked root, which a new publication replaces.</returns>
    public async Task<FileSystemPath?> ReadPublishedContentRootAsync(CancellationToken cancellationToken)
    {
        using var publication = await this.Publication.AcquireReadAsync(this.Context, cancellationToken).ConfigureAwait(true);
        return publication.FindProjectRoot("Content");
    }

    /// <summary>Creates the project's preview session on an engine, as the workspace does.</summary>
    /// <param name="engine">The engine.</param>
    /// <returns>The session, not started.</returns>
    public PreviewSession CreateSession(IEngineService engine)
    {
        var results = new Mock<IOperationResultPublisher>();
        _ = results.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Add);
        return new PreviewSession(engine, this.Publication, new CookedContentMountService(), this.Catalog.Object, this.coordinator, this.Projects, this.manager, results.Object, new OperationStatusReducer());
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.native.Dispose();
        this.coordinator.Dispose();
        try
        {
            this.directory.Delete(recursive: true);
        }
        catch (IOException)
        {
            // A reader the engine still holds keeps a file; the temporary folder is left behind.
        }
    }
}
