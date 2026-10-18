// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Authoring;

/// <summary>
/// New Script, New Input File and New Scene: asks for a name and folder, has the content pipeline create the file, then
/// selects it and opens it: a script or input file in the code editor, a scene in the scene editor.
/// </summary>
/// <param name="dialogs">The dialog service.</param>
/// <param name="projects">The active project.</param>
/// <param name="browserState">The Content Browser state, whose selected folder is the default destination.</param>
/// <param name="authoring">Proposes and checks the name and folder, and writes the file.</param>
/// <param name="codeEditor">The code editor service.</param>
/// <param name="messenger">The messenger that selects the new asset and opens a new scene.</param>
/// <param name="operationResults">The operation-result publisher.</param>
public sealed class AuthoredAssetCreation(
    IDialogService dialogs,
    IProjectContextService projects,
    ContentBrowserState browserState,
    IAssetAuthoring authoring,
    ICodeEditorService codeEditor,
    IMessenger messenger,
    IOperationResultPublisher operationResults)
{
    /// <summary>Asks for a name and folder and creates a script from the template.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<FileSystemPath?> CreateScriptAsync()
        => this.CreateAsync(new CreationKind("New Script", AuthoredAssetKind.Script, ScriptOperationKinds.Create, ScriptDiagnosticCodes.CreateFailed, FailureDomain.ScriptAuthoring), this.OpenInCodeEditorAsync);

    /// <summary>Asks for a name and folder and creates an input file with one action and one mapping context.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<FileSystemPath?> CreateInputFileAsync()
        => this.CreateAsync(new CreationKind("New Input File", AuthoredAssetKind.InputFile, ScriptOperationKinds.CreateInputFile, ScriptDiagnosticCodes.CreateFailed, FailureDomain.ScriptAuthoring), this.OpenInCodeEditorAsync);

    /// <summary>Asks for a name and folder, creates an empty scene, and opens it.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<FileSystemPath?> CreateSceneAsync()
        => this.CreateAsync(new CreationKind("New Scene", AuthoredAssetKind.Scene, SceneOperationKinds.Create, SceneDiagnosticCodes.CreateFailed, FailureDomain.Document), this.OpenSceneAsync);

    /// <summary>Asks for a name and folder, creates a new material there and opens it.</summary>
    /// <returns>The new material's path, or <see langword="null"/> when nothing was created.</returns>
    public Task<FileSystemPath?> CreateMaterialAsync()
        => this.CreateAsync(new CreationKind("New Material", AuthoredAssetKind.Material, MaterialOperationKinds.Create, MaterialDiagnosticCodes.NameInvalid, FailureDomain.MaterialAuthoring), this.OpenMaterialAsync);

    private static string ReadFolder(TextBox folderBox) => "/" + folderBox.Text.Trim().Replace('\\', '/').Trim('/');

    private async Task<FileSystemPath?> CreateAsync(CreationKind kind, Func<ProjectContext, FileSystemPath, Task> open)
    {
        if (projects.ActiveProject is not { } project)
        {
            return null;
        }

        var proposal = await authoring.ProposeAsync(project, kind.Kind, browserState.SelectedFolders.Order().FirstOrDefault(), CancellationToken.None).ConfigureAwait(true);
        var nameBox = new TextBox { Header = "Name", Text = proposal.Name, MinWidth = 320 };
        nameBox.Loaded += (_, _) =>
        {
            _ = nameBox.Focus(FocusState.Programmatic);
            nameBox.SelectAll();
        };
        var folderBox = new TextBox { Header = "Folder", Text = proposal.Folder.VirtualPath };
        var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed };
        error.Foreground = Application.Current.Resources.TryGetValue("SystemFillColorCriticalBrush", out var brush) ? brush as Microsoft.UI.Xaml.Media.Brush : null;

        var spec = new DialogSpec(kind.Title, new StackPanel { Spacing = 12, Children = { nameBox, folderBox, error } })
        {
            PrimaryButtonText = "Create",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = async () =>
            {
                var folder = ReadFolder(folderBox);
                var invalid = VirtualPath.Validate(folder)?.ToMessage()
                    ?? await authoring.ValidateAsync(project, kind.Kind, nameBox.Text.Trim(), AssetPath.Parse(folder), CancellationToken.None).ConfigureAwait(true);
                error.Text = invalid ?? string.Empty;
                error.Visibility = invalid is null ? Visibility.Collapsed : Visibility.Visible;
                return invalid is null;
            },
        };

        if (await dialogs.ShowAsync(spec).ConfigureAwait(true) != DialogButton.Primary)
        {
            return null;
        }

        FileSystemPath path;
        try
        {
            path = await authoring.CreateAsync(project, kind.Kind, nameBox.Text.Trim(), AssetPath.Parse(ReadFolder(folderBox)), CancellationToken.None).ConfigureAwait(true);
        }
        catch (AssetCreationException failure)
        {
            ScriptOperationResults.PublishFailure(operationResults, kind.OperationKind, kind.FailureCode, kind.Title + " failed", failure.Message, exception: failure.InnerException, domain: kind.Domain);
            return null;
        }

        ScriptOperationResults.PublishSuccess(operationResults, kind.OperationKind, kind.Title, $"Created {RelocationPaths.ToVirtual(project, path)}.", path.FullPath);
        await this.RevealAsync(project, path).ConfigureAwait(true);
        await open(project, path).ConfigureAwait(true);
        return path;
    }

    private async Task OpenInCodeEditorAsync(ProjectContext project, FileSystemPath path) => _ = await codeEditor.OpenAsync(path.FullPath).ConfigureAwait(true);

    private async Task OpenSceneAsync(ProjectContext project, FileSystemPath path)
    {
        if (project.FindByFilePath(path) is not { } location)
        {
            return;
        }

        var request = messenger.Send(new OpenSceneRequestMessage(location.Identity));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
        }
    }

    private Task OpenMaterialAsync(ProjectContext project, FileSystemPath path)
    {
        if (project.FindByFilePath(path) is { } location)
        {
            _ = messenger.Send(new OpenMaterialRequestMessage(location.Identity, AssetFileTypes.RemoveSuffix(path.Name)));
        }

        return Task.CompletedTask;
    }

    private async Task RevealAsync(ProjectContext project, FileSystemPath path)
    {
        if (project.FindByFilePath(path) is not { } location)
        {
            return;
        }

        // The authoring service reported the file before it returned, so the feed is already reading it again and the
        // reveal resolves the new row, not a missing one.
        var request = messenger.Send(new ShowAssetRequestMessage(project, location.Identity));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
        }
    }

    private sealed record CreationKind(string Title, AuthoredAssetKind Kind, string OperationKind, string FailureCode, FailureDomain Domain);
}
