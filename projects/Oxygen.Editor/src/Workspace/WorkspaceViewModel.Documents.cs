// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Workspace;

/// <content>The window's documents: their kinds and editors, its document area and the Shell's document service.</content>
public partial class WorkspaceViewModel
{
    // Each file opens once, as a document of the kind registered for its suffix, shown by that kind's editor.
    // A workspace belongs to one project: activating another project closes every window first.
    private static void RegisterDocuments(IContainer childContainer)
    {
        childContainer.RegisterDelegate(
            resolver => new SceneDocumentKind(
                resolver.Resolve<IProjectContextService>().ActiveProject
                    ?? throw new InvalidOperationException("A workspace opens only with a project."),
                resolver.Resolve<ISceneNodeInteraction>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IAssetRedirects>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        childContainer.RegisterDelegate(
            resolver => new OpenDocuments(
                [resolver.Resolve<SceneDocumentKind>(), resolver.Resolve<MaterialDocumentKind>()],
                resolver.Resolve<IAtomicFileStore>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);

        // Each kind's editor comes from the module that edits it.
        childContainer.RegisterDelegate(
            resolver => new DocumentArea(
                resolver.Resolve<IDocumentTabs>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IEnumerable<IDocumentEditorProvider>>().Select(static provider => provider.Editor),
                resolver.Resolve<WindowId>(),
                resolver.Resolve<IWindowManagerService>().ActivateWindow,
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        childContainer.RegisterMapping<IDocumentArea, DocumentArea>();
        RegisterWindowDocumentServices(childContainer);

        // The project's files and relocations, as documents see them.
        childContainer.Register<IDocumentFiles, ProjectDocumentFiles>(Reuse.Singleton);
        childContainer.Register<IDocumentRelocations, DocumentRelocationFeed>(Reuse.Singleton);
        childContainer.Register<DocumentService>(Reuse.Singleton);
        childContainer.RegisterMapping<IDocumentService, DocumentService>();
        childContainer.Register<DocumentAreaViewModel>(Reuse.Singleton);
        childContainer.Register<DocumentAreaView>(Reuse.Singleton);

        // The cook, Play, relocations and the asset feed ask which documents are unsaved.
        childContainer.Register<IUnsavedDocuments, UnsavedDocuments>(Reuse.Singleton);
    }

    // The prompts, confirmations and input of the window, which act on it without features naming it.
    private static void RegisterWindowDocumentServices(IContainer childContainer)
    {
        childContainer.RegisterDelegate<IDocumentClosePrompt>(
            resolver => new DocumentClosePrompt(resolver.Resolve<IDialogService>(), resolver.Resolve<IWindowManagerService>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
        childContainer.RegisterDelegate<IDocumentConfirmations>(
            resolver => new DocumentConfirmations(resolver.Resolve<IDialogService>(), resolver.Resolve<IWindowManagerService>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
        childContainer.RegisterDelegate<IDocumentInputCommitter>(
            resolver => new DocumentInputCommitter(resolver.Resolve<IWindowManagerService>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
        childContainer.RegisterDelegate(
            resolver => new WindowCloseCoordinator(resolver.Resolve<IWindowManagerService>(), resolver.Resolve<IDocumentTabs>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
    }
}
