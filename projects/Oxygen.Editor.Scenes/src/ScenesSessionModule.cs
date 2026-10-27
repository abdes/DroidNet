// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes;

/// <summary>
///     Scene authoring in a project session's scope: the scene document kind, the hidden and locked nodes that scene
///     commands obey, stored per project, the selection of each scene document, whichever window shows it, and the
///     session's clipboard of scene nodes.
/// </summary>
public sealed class ScenesSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // Editor hide / lock workspace state: one owner, project-scoped typed settings, serialized writes.
        scope.RegisterDelegate(
            resolver => new SceneNodeInteraction(
                resolver.Resolve<IProjectSession>(),
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ISceneStructures>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);

        // Scene commands obey the same hide / lock state through the Scenes interface.
        scope.RegisterMapping<ISceneNodeInteraction, SceneNodeInteraction>();

        // Scene files open as documents of the session, with its hide / lock state, its asset moves and the import records
        // of its models, which repairing a kept change in its model writes and reimports.
        scope.Register<IModelImportRecordFiles, ProjectModelRecords>(Reuse.Singleton);
        scope.Register<SceneDocumentKind>(Reuse.Singleton);
        scope.Register<ISceneSelectionService, SceneSelectionService>(Reuse.Singleton);
        scope.Register<ISceneNodeClipboard, SceneNodeClipboard>(Reuse.Singleton);
    }
}
