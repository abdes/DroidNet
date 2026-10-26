// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>
/// The real cook of a workspace: the installed ImportTool behind the native pipeline API, and the content pipeline
/// service over both. A test names only what it varies, so a new dependency of the service changes this builder alone.
/// </summary>
internal sealed class NativeCook
{
    private NativeCook(EditorNativeCompatibilityService compatibility, ImportToolContentPipelineApi api, ContentPipelineService service)
    {
        this.Compatibility = compatibility;
        this.Api = api;
        this.Service = service;
    }

    /// <summary>Gets the compatibility service that checks the installed native tools before they run.</summary>
    public EditorNativeCompatibilityService Compatibility { get; }

    /// <summary>Gets the native pipeline API over the installed ImportTool.</summary>
    public ImportToolContentPipelineApi Api { get; }

    /// <summary>Gets the content pipeline service that cooks the workspace.</summary>
    public ContentPipelineService Service { get; }

    /// <summary>Creates the cook of a workspace.</summary>
    /// <param name="workspace">The workspace whose project is cooked.</param>
    /// <param name="runner">Runs the ImportTool process; a real process runner when none is given.</param>
    /// <param name="publication">Publishes the cooked roots; the workspace's own when none is given.</param>
    /// <param name="generator">Writes scene descriptors; the real generator when none is given.</param>
    /// <returns>The cook.</returns>
    public static NativeCook Create(
        CookWorkspace workspace,
        IContentPipelineProcessRunner? runner = null,
        CookPublicationService? publication = null,
        ISceneDescriptorGenerator? generator = null)
    {
        ArgumentNullException.ThrowIfNull(workspace);
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner ?? new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        return new(compatibility, api, CookScenario.CreateService(workspace, api, generator, compatibility, publication));
    }
}
