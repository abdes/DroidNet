// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Config;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Workspace;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The scene editor, contributed for scene documents. A window shows one scene: opening another one replaces it, and the
/// scene's tab does not close on its own.
/// </summary>
/// <param name="kind">The scene document kind.</param>
/// <param name="container">The window's container, from which each editor gets its services.</param>
public sealed class SceneEditorProvider(SceneDocumentKind kind, IContainer container) : IDocumentEditorProvider
{
    /// <inheritdoc/>
    public DocumentEditorDescriptor Editor { get; } = new(kind, tab => Create(container, tab)) { IsClosable = false, IsOnePerArea = true };

    private static SceneEditorViewModel Create(IContainer container, DocumentTab tab)
        => new(
            tab,
            container.Resolve<IDocumentArea>(),
            container.Resolve<WindowId>(),
            container.Resolve<IEngineService>(),
            container.Resolve<IScenePreview>(),
            container.Resolve<IDocumentInputCommitter>(),
            container.Resolve<IOperationResultPublisher>(),
            container.Resolve<IStatusReducer>(),
            container.Resolve<IContentPipelineService>(),
            container,
            container.Resolve<IMessenger>(),
            container.Resolve<PreviewSettingsService>(),
            container.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault),
            engineSettings: container.Resolve<ISettingsService<IEngineStartupSettings>>(IfUnresolved.ReturnDefault));
}
