// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// New Script and New Input File: asks for a name and folder, writes the template, selects the file and opens it in
/// the code editor.
/// </summary>
/// <param name="dialogs">The dialog service.</param>
/// <param name="projects">The active project.</param>
/// <param name="browserState">The Content Browser state, whose selected folder is the default destination.</param>
/// <param name="codeEditor">The code editor service.</param>
/// <param name="messenger">The messenger that refreshes and selects the new asset.</param>
/// <param name="operationResults">The operation-result publisher.</param>
public sealed class ScriptAssetCreation(
    IDialogService dialogs,
    IProjectContextService projects,
    ContentBrowserState browserState,
    ICodeEditorService codeEditor,
    IMessenger messenger,
    IOperationResultPublisher operationResults)
{
    /// <summary>The longest input file name: the template names its mapping context after the file.</summary>
    internal const int MaxInputNameLength = 63;

    /// <summary>Asks for a name and folder and creates a script from the template.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<string?> CreateScriptAsync()
        => this.CreateAsync(
            new CreationKind("New Script", "Scripts", "NewScript", [ScriptSources.ScriptExtension, ".luau"], ScriptOperationKinds.Create),
            static _ => ScriptTemplates.Script);

    /// <summary>Asks for a name and folder and creates an input file with one action and one mapping context.</summary>
    /// <returns>The new file, or <see langword="null"/> when cancelled or failed.</returns>
    public Task<string?> CreateInputFileAsync()
        => this.CreateAsync(
            new CreationKind("New Input File", "Input", "NewInput", [ScriptSources.InputFileSuffix, ScriptSources.InputActionFileSuffix], ScriptOperationKinds.CreateInputFile),
            ScriptTemplates.InputFile);

    /// <summary>Gets the folder a new file goes to: the folder being browsed, or the kind's folder at a mount root.</summary>
    /// <param name="project">The project.</param>
    /// <param name="selectedFolder">The Content Browser's selected folder, a virtual path such as
    /// <c>/Content/Player</c>, if any.</param>
    /// <param name="kindFolder">The kind's folder name, such as <c>Scripts</c>.</param>
    /// <returns>The virtual folder.</returns>
    internal static string GetDefaultFolder(ProjectContext project, string? selectedFolder, string kindFolder)
    {
        var normalized = selectedFolder?.Replace('\\', '/').Trim().TrimEnd('/');
        var selected = string.IsNullOrWhiteSpace(normalized) || normalized == "/"
            ? null
            : RelocationPaths.ToPhysical(project, "/" + normalized.TrimStart('/')) is { } physical
                ? RelocationPaths.ToVirtual(project, physical)
                : null;
        if (selected is not null && selected.Trim('/').Contains('/', StringComparison.Ordinal))
        {
            return selected;
        }

        var mount = selected ?? "/" + (RelocationPaths.GetWritableMounts(project).FirstOrDefault()?.Name ?? Oxygen.Editor.Projects.Constants.ContentFolderName);
        return RelocationPaths.Combine(mount, kindFolder);
    }

    /// <summary>Checks a proposed name and folder.</summary>
    /// <param name="project">The project.</param>
    /// <param name="name">The name, without extension.</param>
    /// <param name="folder">The virtual folder.</param>
    /// <param name="extensions">Every extension of the kind; a file with any of them and the same name collides.</param>
    /// <param name="maxNameLength">The longest accepted name.</param>
    /// <returns>The reason the name is rejected, or <see langword="null"/> when it is accepted.</returns>
    internal static string? Validate(ProjectContext project, string name, string folder, IReadOnlyList<string> extensions, int maxNameLength)
    {
        if (RelocationPaths.ValidateName(name) is { } invalid)
        {
            return invalid;
        }

        if (name.Length > maxNameLength)
        {
            return string.Create(CultureInfo.CurrentCulture, $"Use at most {maxNameLength} characters.");
        }

        if (RelocationPaths.ToPhysical(project, folder) is not { } physical)
        {
            return "Choose a folder inside a project content folder, such as /Content/Scripts.";
        }

        var existing = extensions.Select(extension => name + extension).FirstOrDefault(file => File.Exists(Path.Combine(physical, file)));
        return existing is null
            ? null
            : $"'{existing}' already exists in {folder}; both would cook to the same asset. Choose another name.";
    }

    private static string NextFreeName(ProjectContext project, string folder, CreationKind kind)
    {
        for (var index = 1; index < 1000; index++)
        {
            var candidate = string.Create(CultureInfo.InvariantCulture, $"{kind.DefaultName}{index}");
            if (Validate(project, candidate, folder, kind.Extensions, MaxInputNameLength) is null)
            {
                return candidate;
            }
        }

        return kind.DefaultName;
    }

    private async Task<string?> CreateAsync(CreationKind kind, Func<string, string> template)
    {
        if (projects.ActiveProject is not { } project)
        {
            return null;
        }

        var folder = GetDefaultFolder(project, browserState.SelectedFolders.FirstOrDefault(), kind.Folder);
        var nameBox = new TextBox { Header = "Name", Text = NextFreeName(project, folder, kind), MinWidth = 320 };
        nameBox.Loaded += (_, _) =>
        {
            _ = nameBox.Focus(FocusState.Programmatic);
            nameBox.SelectAll();
        };
        var folderBox = new TextBox { Header = "Folder", Text = folder };
        var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed };
        error.Foreground = Application.Current.Resources.TryGetValue("SystemFillColorCriticalBrush", out var brush) ? brush as Microsoft.UI.Xaml.Media.Brush : null;

        string? path = null;
        var spec = new DialogSpec(kind.Title, new StackPanel { Spacing = 12, Children = { nameBox, folderBox, error } })
        {
            PrimaryButtonText = "Create",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = () =>
            {
                var name = nameBox.Text.Trim();
                var target = "/" + folderBox.Text.Trim().Replace('\\', '/').Trim('/');
                var invalid = Validate(project, name, target, kind.Extensions, MaxInputNameLength);
                error.Text = invalid ?? string.Empty;
                error.Visibility = invalid is null ? Visibility.Collapsed : Visibility.Visible;
                path = invalid is null ? Path.Combine(RelocationPaths.ToPhysical(project, target)!, name + kind.Extensions[0]) : null;
                return Task.FromResult(invalid is null);
            },
        };

        if (await dialogs.ShowAsync(spec).ConfigureAwait(true) != DialogButton.Primary || path is null)
        {
            return null;
        }

        try
        {
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await using (var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                var bytes = Encoding.UTF8.GetBytes(template(Path.GetFileName(ScriptSources.StripSourceExtension(path))));
                await stream.WriteAsync(bytes).ConfigureAwait(true);
            }
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
        {
            ScriptOperationResults.PublishFailure(operationResults, kind.OperationKind, ScriptDiagnosticCodes.CreateFailed, kind.Title + " failed", $"Could not write '{path}': {failure.Message}", path, failure);
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

        var identity = ScriptSources.IsScriptSource(virtualPath) ? ScriptSources.ToCookedScriptPath(virtualPath) : virtualPath;
        var uri = new Uri($"{AssetUris.Scheme}://{identity}");
        _ = messenger.Send(new AssetsChangedMessage(uri));
        var request = messenger.Send(new ShowAssetRequestMessage(project, uri));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
        }
    }

    private sealed record CreationKind(string Title, string Folder, string DefaultName, IReadOnlyList<string> Extensions, string OperationKind);
}
