// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <content>The workspace window's documents: their kinds, their editors and its document area.</content>
public partial class WorkspaceViewModel
{
    // Each file opens once, as a document of the kind registered for its suffix, shown by that kind's editor.
    // A workspace belongs to one project: activating another project closes every window first.
    private static void RegisterDocuments(IContainer childContainer)
    {
        childContainer.RegisterDelegate(
            resolver => new SceneDocumentKind(
                resolver.Resolve<IProjectManagerService>().CurrentProject
                    ?? throw new InvalidOperationException("A workspace opens only with a project."),
                resolver.Resolve<ISceneNodeInteraction>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IAssetRedirects>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        childContainer.Register<MaterialDocumentKind>(Reuse.Singleton);
        childContainer.Register<MaterialEdits>(Reuse.Singleton);
        childContainer.RegisterDelegate(
            resolver => new OpenDocuments(
                [resolver.Resolve<SceneDocumentKind>(), resolver.Resolve<MaterialDocumentKind>()],
                resolver.Resolve<IAtomicFileStore>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);

        // A scene's tab stays: opening another scene replaces it.
        childContainer.RegisterDelegate(
            resolver => new DocumentEditorDescriptor(resolver.Resolve<SceneDocumentKind>(), tab => CreateSceneEditor(childContainer, tab)) { IsClosable = false },
            Reuse.Singleton);
        childContainer.RegisterDelegate(
            resolver => new DocumentEditorDescriptor(resolver.Resolve<MaterialDocumentKind>(), tab => CreateMaterialEditor(childContainer, tab)),
            Reuse.Singleton);
        childContainer.RegisterDelegate<IDocumentArea>(
            resolver => new DocumentArea(
                resolver.Resolve<IEditorDocumentService>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IEnumerable<DocumentEditorDescriptor>>(),
                resolver.Resolve<WindowId>(),
                resolver.Resolve<IWindowManagerService>().ActivateWindow,
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);

        // The cook knows every open document from the moment it opens.
        childContainer.RegisterDelegate(
            resolver => new OpenDocumentCookInputs(resolver.Resolve<OpenDocuments>(), resolver.Resolve<ICookDocumentRegistry>()),
            Reuse.Singleton);
    }

    private static SceneEditorViewModel CreateSceneEditor(IContainer container, DocumentTab tab)
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
            conflictPrompt: container.Resolve<IDocumentConflictPrompt>(),
            catalog: container.Resolve<IAssetCatalog>(IfUnresolved.ReturnDefault),
            engineSettings: container.Resolve<ISettingsService<IEngineStartupSettings>>(IfUnresolved.ReturnDefault));

    private static MaterialEditorViewModel CreateMaterialEditor(IContainer container, DocumentTab tab)
        => new(
            tab,
            container.Resolve<MaterialEdits>(),
            container.Resolve<IAssetFeed>(),
            container.Resolve<HostingContext>().DispatcherScheduler,
            container.Resolve<IMaterialCookService>(),
            container.Resolve<IProjectContextService>(),
            container.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault),
            inputCommitter: container.Resolve<IDocumentInputCommitter>(),
            windowId: container.Resolve<WindowId>(),
            conflictPrompt: container.Resolve<IDocumentConflictPrompt>(),
            relocation: container.Resolve<IAssetRelocationService>(IfUnresolved.ReturnDefault),
            catalog: container.Resolve<IAssetCatalog>(IfUnresolved.ReturnDefault));
}
