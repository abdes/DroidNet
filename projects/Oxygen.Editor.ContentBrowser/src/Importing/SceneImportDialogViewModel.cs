// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Importing;

/// <summary>Reviews a model's name and authoring destination before import begins.</summary>
public sealed partial class SceneImportDialogViewModel : ObservableObject
{
    private readonly ProjectContext project;
    private readonly IDialogService dialogs;
    private int reviewGeneration;

    /// <summary>Initializes a new instance of the <see cref="SceneImportDialogViewModel"/> class.</summary>
    /// <param name="project">The reviewed project.</param>
    /// <param name="sourcePath">The selected source.</param>
    /// <param name="destination">The initial virtual destination.</param>
    /// <param name="dialogs">The existing folder picker service.</param>
    public SceneImportDialogViewModel(ProjectContext project, string sourcePath, string destination, IDialogService dialogs)
    {
        this.project = project;
        this.dialogs = dialogs;
        this.SourcePath = Path.GetFullPath(sourcePath);
        this.Name = Path.GetFileNameWithoutExtension(sourcePath);
        this.DestinationFolder = destination;
        this.Revalidate();
    }

    /// <summary>Gets the selected source path.</summary>
    public string SourcePath { get; }

    /// <summary>Gets the concise source label.</summary>
    public string SourceName => Path.GetFileName(this.SourcePath);

    /// <summary>Gets or sets the model name.</summary>
    [ObservableProperty]
    public partial string Name { get; set; }

    /// <summary>Gets or sets the virtual authoring destination.</summary>
    [ObservableProperty]
    public partial string DestinationFolder { get; set; }

    /// <summary>Gets or sets inline validation feedback.</summary>
    [ObservableProperty]
    public partial string Error { get; set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether the reviewed choice is valid.</summary>
    [ObservableProperty]
    public partial bool CanAccept { get; set; }

    /// <summary>Gets or sets a value indicating whether a retained source occupies this name.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ReplacementChoiceVisibility))]
    public partial bool HasCollision { get; set; }

    /// <summary>Gets or sets the existing source explicitly selected for replacement.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ReplacementChoiceVisibility), nameof(ReplacementTitle), nameof(ReplacementMessage), nameof(PrimaryButtonText))]
    public partial SceneImportReplacement? Replacement { get; set; }

    /// <summary>Gets the explicit replacement choice visibility.</summary>
    public Visibility ReplacementChoiceVisibility => this.HasCollision && this.Replacement is null ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the compact collision or replacement summary.</summary>
    public string ReplacementTitle => this.Replacement is null ? $"'{this.Name.Trim()}' already exists" : "Replace existing source";

    /// <summary>Gets the existing identity and destination being replaced.</summary>
    public string ReplacementMessage => this.Replacement is { } replacement
        ? Uri.UnescapeDataString(replacement.SourceUri.AbsolutePath) + "\nDestination: " + replacement.Settings.OutputPrefix.TrimEnd('/')
        : "Choose another name, or review the existing source for replacement.";

    /// <summary>Gets the action matching the reviewed import intent.</summary>
    public string PrimaryButtonText => this.Replacement is null ? "Import" : "Replace and import";

    /// <summary>Gets the retained source location shown before import.</summary>
    public string SourceLocation => this.IsProjectSource()
        ? "Source files stay in their current project folder."
        : "Source files: /Content/SourceMedia/DCC/" + this.Name.Trim();

    /// <summary>Gets the reviewed request, or null while validation fails.</summary>
    public SceneImportRequest? Request => this.CanAccept ? new(this.project, this.SourcePath, this.Name.Trim(), ToUri(this.DestinationFolder)) { Replacement = this.Replacement } : null;

    /// <summary>Rechecks the review before accepting the dialog.</summary>
    /// <returns>Whether import may proceed.</returns>
    public bool Validate()
    {
        this.Revalidate();
        return this.CanAccept;
    }

    private static Uri ToUri(string folder) => new("asset:///" + string.Join('/', folder.Trim().Trim('/').Split('/').Select(Uri.EscapeDataString)));

    partial void OnNameChanged(string value)
    {
        this.ClearReplacement();
        this.Revalidate();
        this.OnPropertyChanged(nameof(this.SourceLocation));
        this.OnPropertyChanged(nameof(this.ReplacementTitle));
    }

    partial void OnDestinationFolderChanged(string value)
    {
        this.ClearReplacement();
        this.Revalidate();
    }

    [RelayCommand]
    private async Task ReviewReplacementAsync(CancellationToken cancellationToken)
    {
        var generation = ++this.reviewGeneration;
        try
        {
            this.Error = string.Empty;
            var replacement = await SceneImportReplacement.ReviewAsync(this.project, this.Name.Trim(), cancellationToken).ConfigureAwait(true);
            if (generation != this.reviewGeneration)
            {
                return;
            }

            var directory = Path.GetDirectoryName(replacement.Settings.OutputDirectory)?.Replace('\\', '/');
            this.DestinationFolder = "/" + replacement.Settings.MountPoint + (string.IsNullOrEmpty(directory) ? string.Empty : "/" + directory);
            this.Replacement = replacement;
            this.Revalidate();
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Closing the review does not submit an import.
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or System.Text.Json.JsonException or InvalidOperationException)
        {
            if (generation == this.reviewGeneration)
            {
                this.Error = error.Message;
            }
        }
    }

    private void ClearReplacement()
    {
        this.reviewGeneration++;
        this.Replacement = null;
    }

    [RelayCommand]
    private async Task BrowseDestinationAsync(CancellationToken cancellationToken)
    {
        try
        {
            var selected = await this.dialogs.PickFolderAsync(cancellationToken).ConfigureAwait(true);
            if (selected is null)
            {
                return;
            }

            foreach (var mount in this.project.AuthoringMounts)
            {
                var root = Path.GetFullPath(Path.Combine(this.project.ProjectRoot, mount.RelativePath));
                var relative = Path.GetRelativePath(root, selected).Replace('\\', '/');
                if (!Path.IsPathRooted(relative) && !string.Equals(relative, "..", StringComparison.Ordinal) && !relative.StartsWith("../", StringComparison.Ordinal))
                {
                    this.DestinationFolder = "/" + mount.Name + (string.Equals(relative, ".", StringComparison.Ordinal) ? string.Empty : "/" + relative);
                    this.Revalidate();
                    return;
                }
            }

            this.DestinationFolder = selected;
            this.Error = "Choose a folder in one of this project's authoring mounts.";
            this.CanAccept = false;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling the picker keeps the review open.
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.Error = "Could not select the destination folder. " + error.Message;
        }
    }

    private bool IsProjectSource() => this.project.AuthoringMounts.Any(mount => this.SourcePath.StartsWith(
        Path.GetFullPath(Path.Combine(this.project.ProjectRoot, mount.RelativePath)).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase));

    private void Revalidate()
    {
        this.CanAccept = false;
        this.HasCollision = false;
        try
        {
            var name = this.Name?.Trim() ?? string.Empty;
            _ = SceneImportTarget.Resolve(this.project, ToUri(this.DestinationFolder ?? string.Empty), name);
            var content = this.project.AuthoringMounts.FirstOrDefault(static mount => string.Equals(mount.Name, "Content", StringComparison.OrdinalIgnoreCase));
            if (!this.IsProjectSource() && content is not null && Directory.Exists(Path.Combine(this.project.ProjectRoot, content.RelativePath, "SourceMedia", "DCC", name)))
            {
                this.HasCollision = true;
                this.Error = string.Empty;
                this.CanAccept = this.Replacement is not null;
                return;
            }

            if (this.Replacement is not null)
            {
                this.Error = "The reviewed source is no longer available. Choose another name or review the source again.";
                return;
            }

            this.Error = string.Empty;
            this.CanAccept = true;
        }
        catch (Exception error) when (error is ArgumentException or InvalidOperationException)
        {
            this.Error = error.Message;
        }
    }
}
