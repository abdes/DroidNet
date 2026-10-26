// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
///     The content pipeline in the editor scope: what no project owns, the SDK's engine content, import manifests and
///     the native import tool, and the recovery of a project before its session opens.
/// </summary>
/// <remarks>
///     A project's cook, its publication and its relocations are its session's
///     (<see cref="ContentPipelineSessionModule" />).
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

        // Before a project's session opens, its interrupted changes are restored.
        scope.Register<CookPublicationRecovery>(Reuse.Singleton);
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
        scope.Register<Import.IModelSourceAnalyzer, Import.ModelSourceAnalyzer>(Reuse.Singleton);
    }
}
