// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using Microsoft.UI;
using Microsoft.Windows.Storage.Pickers;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     The desktop storage pickers of <see cref="DialogService"/>.
/// </summary>
public sealed partial class DialogService
{
    /// <inheritdoc/>
    public Task<string?> PickFolderAsync(FolderPickerSpec picker, CancellationToken cancellationToken = default)
        => this.PickFolderAsync(picker, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<string?> PickFolderAsync(FolderPickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.PickFolderAsync(picker, (WindowId?)ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public Task<string?> PickFileAsync(FilePickerSpec picker, CancellationToken cancellationToken = default)
        => this.PickFileAsync(picker, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<string?> PickFileAsync(FilePickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.PickFileAsync(picker, (WindowId?)ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public Task<IReadOnlyList<string>> PickFilesAsync(FilePickerSpec picker, CancellationToken cancellationToken = default)
        => this.PickFilesAsync(picker, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<IReadOnlyList<string>> PickFilesAsync(FilePickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.PickFilesAsync(picker, (WindowId?)ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public Task<string?> PickSaveFileAsync(FileSavePickerSpec picker, CancellationToken cancellationToken = default)
        => this.PickSaveFileAsync(picker, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<string?> PickSaveFileAsync(FileSavePickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.PickSaveFileAsync(picker, (WindowId?)ownerWindowId, cancellationToken);

    /// <summary>
    ///     Creates the native folder picker for a spec.
    /// </summary>
    /// <param name="spec">The picker specification.</param>
    /// <param name="ownerWindowId">The window that owns the picker.</param>
    /// <returns>The configured picker.</returns>
    internal static FolderPicker CreateFolderPicker(FolderPickerSpec spec, WindowId ownerWindowId)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ValidateIdentity(spec.Title, spec.SettingsIdentifier, nameof(spec));
        var picker = new FolderPicker(ownerWindowId)
        {
            Title = spec.Title,
            SettingsIdentifier = spec.SettingsIdentifier,
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
        };
        if (!string.IsNullOrWhiteSpace(spec.SuggestedStartFolder))
        {
            picker.SuggestedStartFolder = spec.SuggestedStartFolder;
        }

        return picker;
    }

    /// <summary>
    ///     Creates the native file-open picker for a spec.
    /// </summary>
    /// <param name="spec">The picker specification.</param>
    /// <param name="ownerWindowId">The window that owns the picker.</param>
    /// <returns>The configured picker, with normalized extensions.</returns>
    internal static FileOpenPicker CreateFilePicker(FilePickerSpec spec, WindowId ownerWindowId)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ValidateIdentity(spec.Title, spec.SettingsIdentifier, nameof(spec));
        var choices = NormalizeFileTypeChoices(spec.FileTypeChoices, nameof(spec));
        var picker = new FileOpenPicker(ownerWindowId)
        {
            Title = spec.Title,
            SettingsIdentifier = spec.SettingsIdentifier,
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
            ViewMode = PickerViewMode.List,
        };
        if (!string.IsNullOrWhiteSpace(spec.SuggestedStartFolder))
        {
            picker.SuggestedStartFolder = spec.SuggestedStartFolder;
        }

        foreach (var (label, extensions) in choices)
        {
            picker.FileTypeChoices.Add(label, extensions);
        }

        return picker;
    }

    /// <summary>
    ///     Creates the native file-save picker for a spec. Its default extension is the first extension of the first
    ///     file type choice.
    /// </summary>
    /// <param name="spec">The picker specification.</param>
    /// <param name="ownerWindowId">The window that owns the picker.</param>
    /// <returns>The configured picker, with normalized extensions.</returns>
    internal static FileSavePicker CreateSavePicker(FileSavePickerSpec spec, WindowId ownerWindowId)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ValidateIdentity(spec.Title, spec.SettingsIdentifier, nameof(spec));
        ArgumentNullException.ThrowIfNull(spec.SuggestedFileName, nameof(spec));
        var choices = NormalizeFileTypeChoices(spec.FileTypeChoices, nameof(spec));
        var picker = new FileSavePicker(ownerWindowId)
        {
            Title = spec.Title,
            SettingsIdentifier = spec.SettingsIdentifier,
            SuggestedFileName = spec.SuggestedFileName,
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
            DefaultFileExtension = choices[0].Extensions[0],
        };
        if (!string.IsNullOrWhiteSpace(spec.SuggestedStartFolder))
        {
            picker.SuggestedStartFolder = spec.SuggestedStartFolder;
        }

        foreach (var (label, extensions) in choices)
        {
            picker.FileTypeChoices.Add(label, extensions);
        }

        return picker;
    }

    /// <summary>
    ///     Normalizes a file extension to the <c>.ext</c> form the native pickers require.
    /// </summary>
    /// <param name="extension">An extension written <c>.ext</c>, <c>ext</c> or <c>*.ext</c>.</param>
    /// <returns>The extension with a single leading dot.</returns>
    /// <exception cref="ArgumentException">
    ///     The extension is empty, ends with a dot, or contains wildcard, path or whitespace characters.
    /// </exception>
    internal static string NormalizeExtension(string extension)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(extension);
        var name = extension.StartsWith("*.", StringComparison.Ordinal) ? extension[2..]
            : extension.StartsWith('.') ? extension[1..]
            : extension;
        return name.Length == 0
            || name.StartsWith('.') || name.EndsWith('.')
            || name.Any(static c => char.IsWhiteSpace(c) || c is '*' or '?')
            || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            ? throw new ArgumentException($"'{extension}' is not a file extension.", nameof(extension))
            : "." + name;
    }

    private static void ValidateIdentity(string title, string settingsIdentifier, string paramName)
    {
        if (string.IsNullOrWhiteSpace(title) || string.IsNullOrWhiteSpace(settingsIdentifier))
        {
            throw new ArgumentException("A picker needs a title and a settings identifier.", paramName);
        }
    }

    private static List<(string Label, List<string> Extensions)> NormalizeFileTypeChoices(
        IReadOnlyDictionary<string, IReadOnlyList<string>> fileTypeChoices,
        string paramName)
    {
        if (fileTypeChoices is null || fileTypeChoices.Count == 0)
        {
            throw new ArgumentException("At least one file type choice is required.", paramName);
        }

        var choices = new List<(string Label, List<string> Extensions)>(fileTypeChoices.Count);
        foreach (var (label, extensions) in fileTypeChoices)
        {
            if (string.IsNullOrWhiteSpace(label) || extensions is null || extensions.Count == 0)
            {
                throw new ArgumentException("Each file type choice needs a label and an extension.", paramName);
            }

            choices.Add((label, [.. extensions.Select(NormalizeExtension).Distinct(StringComparer.OrdinalIgnoreCase)]));
        }

        return choices;
    }

    private async Task<string?> PickFileAsync(FilePickerSpec picker, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(picker);
        cancellationToken.ThrowIfCancellationRequested();
        var owner = this.ResolveOwnerWindow(ownerWindowId);
        return await owner.DispatcherQueue.EnqueueAsync(async () =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var nativePicker = CreateFilePicker(picker, owner.Id);
            var file = await nativePicker.PickSingleFileAsync();
            cancellationToken.ThrowIfCancellationRequested();
            return file?.Path;
        }).ConfigureAwait(false);
    }

    private async Task<IReadOnlyList<string>> PickFilesAsync(FilePickerSpec picker, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(picker);
        cancellationToken.ThrowIfCancellationRequested();
        var owner = this.ResolveOwnerWindow(ownerWindowId);
        return await owner.DispatcherQueue.EnqueueAsync(async () =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var nativePicker = CreateFilePicker(picker, owner.Id);
            var files = await nativePicker.PickMultipleFilesAsync();
            cancellationToken.ThrowIfCancellationRequested();
            return (IReadOnlyList<string>)[.. files.Select(static file => file.Path)];
        }).ConfigureAwait(false);
    }

    private async Task<string?> PickSaveFileAsync(FileSavePickerSpec picker, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(picker);
        cancellationToken.ThrowIfCancellationRequested();
        var owner = this.ResolveOwnerWindow(ownerWindowId);
        return await owner.DispatcherQueue.EnqueueAsync(async () =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var nativePicker = CreateSavePicker(picker, owner.Id);
            var file = await nativePicker.PickSaveFileAsync();
            cancellationToken.ThrowIfCancellationRequested();
            return file?.Path;
        }).ConfigureAwait(false);
    }

    private async Task<string?> PickFolderAsync(FolderPickerSpec picker, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(picker);
        cancellationToken.ThrowIfCancellationRequested();
        var owner = this.ResolveOwnerWindow(ownerWindowId);
        return await owner.DispatcherQueue.EnqueueAsync(async () =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var nativePicker = CreateFolderPicker(picker, owner.Id);
            var folder = await nativePicker.PickSingleFolderAsync();
            cancellationToken.ThrowIfCancellationRequested();
            return folder?.Path;
        }).ConfigureAwait(false);
    }
}
