// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview;

/// <summary>
///     The engine in a project session's scope: the preview of the session's open scene documents, the project's
///     preview session, the runtime status of its assets, and Play.
/// </summary>
/// <remarks>
///     The project's preview also applies the content-mount changes the Content Explorer confirms. The player runs the
///     scenes of the session's project and ends with the session; its problems and log reach Problems and Output.
/// </remarks>
public sealed class LivePreviewSessionModule : IScopeModule<ISessionLayer>
{
    // The preview follows the open scene documents; the preview and Play sessions follow the project session.
    private static readonly Type[] Activated =
    [
        typeof(IScenePreview),
        typeof(PreviewSession),
        typeof(PlaySession),
    ];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IAssetRuntimeStatusSource, AssetRuntimeStatusSource>(Reuse.Singleton);
        RegisterPreview(scope);
        RegisterPlay(scope);
    }

    private static void RegisterPreview(IRegistrator scope)
    {
        scope.RegisterDelegate<IScenePreview>(
            resolver => ScenePreviews.Create(
                resolver.Resolve<IEngineService>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IProjectSession>(),
                resolver.Resolve<ILoggerFactory>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IAssetFeed>()),
            Reuse.Singleton);
        scope.Register<PreviewSession>(Reuse.Singleton);
        scope.RegisterMapping<IContentMountChanges, PreviewSession>();
    }

    private static void RegisterPlay(IRegistrator scope)
    {
        scope.Register<PlaySettings>(Reuse.Singleton);
        scope.Register<PlaySession>(Reuse.Singleton);
        scope.RegisterMapping<IRuntimeProblemSource, PlaySession>();
        scope.RegisterMapping<IAssetProblemSource, PlaySession>();
        scope.RegisterMapping<IPlayerLog, PlaySession>();
    }
}
