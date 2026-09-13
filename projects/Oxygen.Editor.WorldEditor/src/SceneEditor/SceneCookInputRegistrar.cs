// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Connects a scene document's saved identity and UI-owned read state to cook capture.</summary>
/// <param name="documents">The shared registry of saved authoring inputs.</param>
/// <param name="projectManager">The owner of acknowledged scene source versions.</param>
/// <param name="hostingContext">The authoring UI dispatcher.</param>
/// <param name="documentService">The existing document metadata notifications.</param>
public sealed partial class SceneCookInputRegistrar(
    ICookDocumentRegistry documents,
    IProjectManagerService projectManager,
    HostingContext hostingContext,
    IDocumentService documentService)
{
    /// <summary>Registers a loaded scene until its document or saved source changes.</summary>
    /// <param name="context">The scene document's current authoring ownership.</param>
    /// <param name="commands">The owner of the document's save gate and gesture state.</param>
    /// <returns>A registration to dispose, or null until the scene has a saved source.</returns>
    public IDisposable? Register(SceneDocumentCommandContext context, ISceneDocumentCommandService commands)
        => projectManager.GetSceneSourceVersion(context.Scene) is { } source
            ? new Registration(context, projectManager, documentService, documents.Register(source.SourcePath, token => hostingContext.Dispatcher.DispatchAsync(() => commands.AcquireCookReadAsync(context, token))))
            : null;

    private sealed partial class Registration : IDisposable
    {
        private readonly SceneDocumentCommandContext context;
        private readonly IProjectManagerService projectManager;
        private readonly ICookDocumentRegistration document;
        private readonly IDocumentService documentService;

        public Registration(SceneDocumentCommandContext context, IProjectManagerService projectManager, IDocumentService documentService, ICookDocumentRegistration document)
        {
            this.context = context;
            this.projectManager = projectManager;
            this.document = document;
            this.documentService = documentService;
            this.documentService.DocumentMetadataChanged += this.OnMetadataChanged;
            this.PublishState();
        }

        public void Dispose()
        {
            this.documentService.DocumentMetadataChanged -= this.OnMetadataChanged;
            this.document.Dispose();
        }

        private void OnMetadataChanged(object? sender, DocumentMetadataChangedEventArgs args)
        {
            if (ReferenceEquals(args.NewMetadata, this.context.Metadata))
            {
                this.PublishState();
            }
        }

        private void PublishState()
        {
            if (this.projectManager.GetSceneSourceVersion(this.context.Scene) is { } source)
            {
                this.document.UpdateState(new(
                    this.context.DocumentId,
                    Path.GetFullPath(source.SourcePath),
                    this.context.Metadata.Title,
                    this.context.Metadata.ChangeVersion,
                    this.context.Metadata.SavedVersion,
                    this.context.Metadata.IsDirty,
                    source.Version.Sha256));
            }
        }
    }
}
