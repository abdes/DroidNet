// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
/// Script and input file commands: New Script, New Input File, Edit and Open with.
/// </summary>
public partial class AssetsViewModel
{
    /// <summary>Gets a value indicating whether New Script and New Input File are available.</summary>
    public bool CanCreateScripts => creation is not null;

    /// <summary>Gets the reason Edit is unavailable for the selection, or a description of what it opens.</summary>
    public string EditToolTip
        => codeEditor is null ? "Code editing is unavailable."
            : this.SelectedAssets.Count != 1 ? "Select one script or input file to edit it in your code editor."
            : this.FindCodeSource(this.SelectedAssets[0]) is { } source ? $"Edit {Path.GetFileName(source.SourcePath)} in your code editor"
            : "Only scripts and input files open in the code editor.";

    /// <summary>Finds the file the code editor opens for an asset.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The file and line, or <see langword="null"/> when the asset has no hand-written source.</returns>
    internal InputDeclaration? FindCodeSource(AssetEntry asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return asset is { Kind: AssetKind.Script or AssetKind.InputFile, SourcePath: { } source } && File.Exists(source.FullPath)
            ? new InputDeclaration(source.FullPath, 1)
            : null;
    }

    private bool CanEditSelected() => codeEditor is not null && this.SelectedAssets.Count == 1 && this.FindCodeSource(this.SelectedAssets[0]) is not null;

    [RelayCommand(CanExecute = nameof(CanEditSelected))]
    private async Task EditSelectedAsync()
    {
        if (codeEditor is not null && this.SelectedAssets is [var asset] && this.FindCodeSource(asset) is { } source)
        {
            _ = await codeEditor.OpenAsync(source.SourcePath, source.Line).ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanEditSelected))]
    private async Task OpenSelectedWithAsync()
    {
        if (codeEditor is not null && this.SelectedAssets is [var asset] && this.FindCodeSource(asset) is { } source)
        {
            _ = await codeEditor.OpenWithAsync(source.SourcePath).ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanCreateScripts))]
    private async Task CreateScriptAsync()
    {
        if (creation is not null)
        {
            _ = await creation.CreateScriptAsync().ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanCreateScripts))]
    private async Task CreateInputFileAsync()
    {
        if (creation is not null)
        {
            _ = await creation.CreateInputFileAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Opens an invoked script or input asset in the code editor.</summary>
    /// <param name="asset">The invoked asset.</param>
    /// <returns><see langword="true"/> when the asset has a hand-written source and the editor was asked to open it.</returns>
    private async Task<bool> TryEditInvokedAsync(AssetEntry asset)
    {
        if (codeEditor is null || this.FindCodeSource(asset) is not { } source)
        {
            return false;
        }

        _ = await codeEditor.OpenAsync(source.SourcePath, source.Line).ConfigureAwait(true);
        return true;
    }

    private void NotifyEditCommands()
    {
        this.EditSelectedCommand.NotifyCanExecuteChanged();
        this.OpenSelectedWithCommand.NotifyCanExecuteChanged();
        this.OnPropertyChanged(nameof(this.EditToolTip));
    }
}
