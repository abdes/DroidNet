// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>Composes the production native pipeline and cooking compatibility checks.</summary>
public sealed partial class NativeContentPipelineFixture
{
    private readonly EditorNativeCompatibilityService compatibility = EditorNativeCompatibilityService.ForCooking();

    /// <summary>Initializes a new instance of the <see cref="NativeContentPipelineFixture"/> class.</summary>
    /// <param name="session">The fixture project's session.</param>
    /// <param name="coordinator">Its shared cook writer.</param>
    public NativeContentPipelineFixture(IProjectSession session, IContentCookCoordinator coordinator)
    {
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.compatibility);
        var storage = new NativeStorageProvider(new RealFileSystem());
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var engineContent = EngineContentPak.Installed();
        this.Publication = new CookPublicationService(
            coordinator,
            session,
            files,
            new ProjectManagerService(storage, atomicFiles: files),
            engineContent);
        this.Pipeline = new ContentPipelineService(
            session,
            coordinator,
            new ProjectCookScopeProvider(storage),
            new SceneDescriptorGenerator(),
            new ContentImportManifestBuilder(),
            new ContentImportManifestValidator(),
            api,
            this.compatibility,
            files,
            this.Publication,
            engineContent);
    }

    /// <summary>Gets the production pipeline for fixture operations.</summary>
    public ContentPipelineService Pipeline { get; }

    /// <summary>Gets the same publication owner used by fixture cooking and runtime admission.</summary>
    public CookPublicationService Publication { get; }
}
