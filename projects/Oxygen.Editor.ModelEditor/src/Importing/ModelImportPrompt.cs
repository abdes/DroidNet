// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Dialogs;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Desktop;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>Shows the Import model dialog for the session's project: the file list, the settings panel and Before
/// import, with the session commands in the button bar and the Import run's settings behind the Import button's
/// chevron.</summary>
/// <param name="session">The project session, whose project the models are imported into.</param>
/// <param name="dialogs">Shows the dialog and its pickers.</param>
/// <param name="analyzer">Analyzes each file added to the dialog.</param>
/// <param name="assets">The project's assets, whose materials the Materials rows offer.</param>
/// <param name="shell">Reveals a file in File Explorer.</param>
public sealed class ModelImportPrompt(
    IProjectSession session,
    IDialogService dialogs,
    IModelSourceAnalyzer analyzer,
    IAssetFeed assets,
    IDesktopIntegration shell) : IModelImportPrompt
{
    /// <inheritdoc />
    public async Task<ModelImportPlan?> ShowAsync(ModelImportPromptRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        var dispatcher = DispatcherQueue.GetForCurrentThread()
            ?? throw new InvalidOperationException("The import dialog opens on the UI thread.");
        using var model = new ModelImportDialogViewModel(session.Context, dialogs, analyzer, request.DestinationFolder, ImportRunSettings.Defaults(), shell);
        using var materials = assets.Items.Subscribe(
            list => dispatcher.TryEnqueue(() => model.Settings.UpdateMaterials(list)),
            static _ =>
            {
                // Without the asset feed, the Materials rows offer no project material; every material is imported.
            });
        _ = model.AddFiles(request.Files);
        var button = await dialogs.ShowAsync(
            new DialogSpec("Import model", new ModelImportDialogView(model))
            {
                ViewModel = model,
                PrimaryButtonText = "Import",
                CloseButtonText = "Cancel",
                DefaultButton = DialogButton.Primary,
                PrimaryAction = _ => Task.FromResult(model.Validate()),
                IsPrimaryButtonEnabled = () => model.CanImport,
                PrimaryButtonOptions = new ImportRunView(model.Run),
                HasModifiedPrimaryButtonOptions = () => model.Run.HasCustomSettings,
                FooterCommands = [new("Open session…", model.OpenSessionCommand), new("Save session…", model.SaveSessionCommand)],
                MaxWidth = 1120,
                MaxHeight = 900,
            },
            cancellationToken).ConfigureAwait(true);
        return button == DialogButton.Primary ? model.ToPlan() : null;
    }
}
