// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes;

/// <summary>
///     Scene authoring in a workspace window's scope: the scene document kind of the window's project, the selection,
///     and the hidden and locked nodes that scene commands obey.
/// </summary>
/// <remarks>
///     A workspace belongs to one project: activating another project closes every window first. The project session
///     owns these services once it exists.
/// </remarks>
public sealed class ScenesWindowModule : IScopeModule<IWindowLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.RegisterDelegate(
            resolver => new SceneDocumentKind(
                resolver.Resolve<IProjectContextService>().ActiveProject
                    ?? throw new InvalidOperationException("A workspace opens only with a project."),
                resolver.Resolve<ISceneNodeInteraction>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IAssetRedirects>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        scope.Register<ISceneSelectionService, SceneSelectionService>(Reuse.Singleton);

        // Editor hide / lock workspace state: one owner, project-scoped typed settings, serialized writes.
        scope.RegisterDelegate(
            resolver => new SceneNodeInteraction(
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);

        // Scene commands obey the same hide / lock state through the Scenes interface.
        scope.RegisterMapping<ISceneNodeInteraction, SceneNodeInteraction>();
    }
}
