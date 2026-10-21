// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>The material editor, which the Material Editor contributes for material documents.</summary>
/// <param name="kind">The material document kind.</param>
/// <param name="edits">Edits material documents and records their history.</param>
/// <param name="assets">The asset feed the editor's pickers and status read.</param>
/// <param name="hosting">The UI thread, where the editor shows the feed's changes.</param>
/// <param name="cook">Cooks a material on request.</param>
/// <param name="session">The project session the window lives in.</param>
/// <param name="inputCommitter">Commits the focused control's input before a save.</param>
/// <param name="documents">
/// The window's document service, which saves the documents a cook reads after asking. It creates the editors, so it is
/// reached once an editor is created.
/// </param>
/// <param name="unsavedInputs">Finds the unsaved documents a cook reads.</param>
/// <param name="loggerFactory">The logger factory, if any.</param>
public sealed class MaterialEditorProvider(
    MaterialDocumentKind kind,
    MaterialEdits edits,
    IAssetFeed assets,
    HostingContext hosting,
    IMaterialCookService cook,
    IProjectSession session,
    IDocumentInputCommitter inputCommitter,
    Lazy<IDocumentService> documents,
    IUnsavedCookInputs unsavedInputs,
    ILoggerFactory? loggerFactory = null) : IDocumentEditorProvider
{
    /// <inheritdoc/>
    public DocumentEditorDescriptor Editor { get; } = new(
        kind,
        tab => new MaterialEditorViewModel(tab, edits, assets, hosting.DispatcherScheduler, cook, session, loggerFactory, inputCommitter, documents.Value, unsavedInputs));
}
