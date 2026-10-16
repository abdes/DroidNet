// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// New Script and New Input File: asks for a name and folder, has the content pipeline create the file, then selects
/// it and opens it in the code editor.
/// </summary>
/// <param name="dialogs">The dialog service.</param>
/// <param name="projects">The active project.</param>
/// <param name="browserState">The Content Browser state, whose selected folder is the default destination.</param>
/// <param name="authoring">Proposes and checks the name and folder, and writes the file.</param>
/// <param name="codeEditor">The code editor service.</param>
/// <param name="messenger">The messenger that selects the new asset.</param>
/// <param name="operationResults">The operation-result publisher.</param>
public sealed class ScriptAssetCreation(
    IDialogService dialogs,
    IProjectContextService projects,
    ContentBrowserState browserState,
    IScriptAssetAuthoring authoring,
    ICodeEditorService codeEditor,
    IMessenger messenger,
    IOperationResultPublisher operationResults)
{
    /// <summary>Asks for a name and folder and creates a script from the template.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<string?> CreateScriptAsync()
        => this.CreateAsync(new CreationKind("New Script", ScriptAssetKind.Script, ScriptOperationKinds.Create));

    /// <summary>Asks for a name and folder and creates an input file with one action and one mapping context.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<string?> CreateInputFileAsync()
        => this.CreateAsync(new CreationKind("New Input File", ScriptAssetKind.InputFile, ScriptOperationKinds.CreateInputFile));

    private static string ReadFolder(TextBox folderBox) => "/" + folderBox.Text.Trim().Replace('\\', '/').Trim('/');

    private async Task<string?> CreateAsync(CreationKind kind)
    {
        if (projects.ActiveProject is not { } project)
        {
            return null;
        }

        var proposal = await authoring.ProposeAsync(project, kind.Kind, browserState.SelectedFolders.FirstOrDefault(), CancellationToken.None).ConfigureAwait(true);
        var nameBox = new TextBox { Header = "Name", Text = proposal.Name, MinWidth = 320 };
        nameBox.Loaded += (_, _) =>
        {
            _ = nameBox.Focus(FocusState.Programmatic);
            nameBox.SelectAll();
        };
        var folderBox = new TextBox { Header = "Folder", Text = proposal.Folder };
        var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed };
        error.Foreground = Application.Current.Resources.TryGetValue("SystemFillColorCriticalBrush", out var brush) ? brush as Microsoft.UI.Xaml.Media.Brush : null;

        var spec = new DialogSpec(kind.Title, new StackPanel { Spacing = 12, Children = { nameBox, folderBox, error } })
        {
            PrimaryButtonText = "Create",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = async () =>
            {
                var invalid = await authoring.ValidateAsync(project, kind.Kind, nameBox.Text.Trim(), ReadFolder(folderBox), CancellationToken.None).ConfigureAwait(true);
                error.Text = invalid ?? string.Empty;
                error.Visibility = invalid is null ? Visibility.Collapsed : Visibility.Visible;
                return invalid is null;
            },
        };

        if (await dialogs.ShowAsync(spec).ConfigureAwait(true) != DialogButton.Primary)
        {
            return null;
        }

        string path;
        try
        {
            path = await authoring.CreateAsync(project, kind.Kind, nameBox.Text.Trim(), ReadFolder(folderBox), CancellationToken.None).ConfigureAwait(true);
        }
        catch (ScriptAssetCreationException failure)
        {
            ScriptOperationResults.PublishFailure(operationResults, kind.OperationKind, ScriptDiagnosticCodes.CreateFailed, kind.Title + " failed", failure.Message, exception: failure.InnerException);
            return null;
        }

        ScriptOperationResults.PublishSuccess(operationResults, kind.OperationKind, kind.Title, $"Created {RelocationPaths.ToVirtual(project, path)}.", path);
        await this.RevealAsync(project, path).ConfigureAwait(true);
        _ = await codeEditor.OpenAsync(path).ConfigureAwait(true);
        return path;
    }

    private async Task RevealAsync(ProjectContext project, string path)
    {
        if (RelocationPaths.ToVirtual(project, path) is not { } virtualPath)
        {
            return;
        }

        var identity = AssetFileTypes.Is(virtualPath, AssetFileCategory.Script) ? AssetFileTypes.ToCookedPath(virtualPath)! : virtualPath;
        var uri = new Uri($"{AssetUris.Scheme}://{identity}");

        // The authoring service reported the file before it returned, so the feed is already reading it again and the
        // reveal resolves the new row, not a missing one.
        var request = messenger.Send(new ShowAssetRequestMessage(project, uri));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
        }
    }

    private sealed record CreationKind(string Title, ScriptAssetKind Kind, string OperationKind);
}
