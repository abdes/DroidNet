// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Importing;

/// <summary>Reviews a standalone image's name, destination and native texture settings.</summary>
public sealed partial class TextureImportDialogViewModel : ObservableObject
{
    private readonly ProjectContext project;
    private readonly IDialogService dialogs;
    private string name = string.Empty;
    private string destinationFolder = string.Empty;
    private string intent = "albedo";
    private string colorSpace = "srgb";
    private string format = "rgba8_srgb";
    private string error = string.Empty;
    private bool canAccept;

    /// <summary>Initializes a reviewed image import.</summary>
    /// <param name="project">The active project.</param>
    /// <param name="sourcePath">The selected image.</param>
    /// <param name="destinationFolder">The initial authoring destination.</param>
    /// <param name="dialogs">The folder picker service.</param>
    public TextureImportDialogViewModel(ProjectContext project, string sourcePath, string destinationFolder, IDialogService dialogs)
    {
        this.project = project ?? throw new ArgumentNullException(nameof(project));
        this.dialogs = dialogs ?? throw new ArgumentNullException(nameof(dialogs));
        this.SourcePath = Path.GetFullPath(sourcePath);
        this.Name = Path.GetFileNameWithoutExtension(sourcePath);
        this.DestinationFolder = destinationFolder;
        this.Revalidate();
    }

    /// <summary>Gets the selected image source.</summary>
    public string SourcePath { get; }

    /// <summary>Gets the native intents available for a standalone image.</summary>
    public IReadOnlyList<string> Intents { get; } = ["albedo", "normal", "roughness", "metallic", "ao", "orm", "emissive", "opacity", "data", "height"];

    /// <summary>Gets supported native color spaces.</summary>
    public IReadOnlyList<string> ColorSpaces { get; } = ["srgb", "linear"];

    /// <summary>Gets supported native texture formats.</summary>
    public IReadOnlyList<string> Formats { get; } = ["rgba8", "rgba8_srgb", "bc7", "bc7_srgb", "rgba16f", "rgba32f"];

    /// <summary>Gets or sets the named texture asset stem.</summary>
    public string Name
    {
        get => this.name;
        set
        {
            if (this.SetProperty(ref this.name, value))
            {
                this.Revalidate();
            }
        }
    }

    /// <summary>Gets or sets the project authoring destination.</summary>
    public string DestinationFolder
    {
        get => this.destinationFolder;
        set
        {
            if (this.SetProperty(ref this.destinationFolder, value))
            {
                this.Revalidate();
            }
        }
    }

    /// <summary>Gets or sets the selected native semantic.</summary>
    public string Intent
    {
        get => this.intent;
        set
        {
            if (this.SetProperty(ref this.intent, value))
            {
                this.Revalidate();
            }
        }
    }

    /// <summary>Gets or sets the selected image decode color space.</summary>
    public string ColorSpace
    {
        get => this.colorSpace;
        set
        {
            if (this.SetProperty(ref this.colorSpace, value))
            {
                this.Revalidate();
            }
        }
    }

    /// <summary>Gets or sets the selected native output format.</summary>
    public string Format
    {
        get => this.format;
        set
        {
            if (this.SetProperty(ref this.format, value))
            {
                this.Revalidate();
            }
        }
    }

    /// <summary>Gets or sets the inline validation failure.</summary>
    public string Error
    {
        get => this.error;
        private set => this.SetProperty(ref this.error, value);
    }

    /// <summary>Gets or sets whether the reviewed import is valid and collision-free.</summary>
    public bool CanAccept
    {
        get => this.canAccept;
        private set => this.SetProperty(ref this.canAccept, value);
    }

    /// <summary>Gets the output virtual path shown before import.</summary>
    public string OutputVirtualPath
    {
        get
        {
            try
            {
                return TextureSourceImportTarget.Resolve(this.CreateRequest()).VirtualPath;
            }
            catch (ArgumentException)
            {
                return string.Empty;
            }
        }
    }

    /// <summary>Gets the validated import request.</summary>
    public TextureSourceImportRequest? Request => this.CanAccept ? this.CreateRequest() : null;

    /// <summary>Rechecks the reviewed request before accepting the dialog.</summary>
    /// <returns>Whether the import may proceed.</returns>
    public bool Validate()
    {
        this.Revalidate();
        return this.CanAccept;
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

            this.Error = "Choose a folder inside one of this project's authoring mounts.";
            this.CanAccept = false;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling the picker keeps the review open.
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.Error = "Could not select the destination folder. " + error.Message;
            this.CanAccept = false;
        }
    }

    private TextureSourceImportRequest CreateRequest()
        => new(this.project, this.SourcePath, ToUri(this.DestinationFolder), this.Name.Trim(), this.Intent, this.ColorSpace, this.Format);

    private static Uri ToUri(string folder)
        => new("asset:///" + string.Join('/', folder.Trim().Trim('/').Split('/').Select(Uri.EscapeDataString)));

    private void Revalidate()
    {
        this.CanAccept = false;
        try
        {
            var target = TextureSourceImportTarget.Resolve(this.CreateRequest());
            if (File.Exists(target.ImagePath) || File.Exists(target.DescriptorPath))
            {
                this.Error = "A texture asset with this name already exists in the destination.";
                this.OnPropertyChanged(nameof(this.OutputVirtualPath));
                return;
            }

            this.Error = string.Empty;
            this.CanAccept = true;
        }
        catch (Exception error) when (error is ArgumentException or IOException or UnauthorizedAccessException)
        {
            this.Error = error.Message;
        }

        this.OnPropertyChanged(nameof(this.OutputVirtualPath));
    }
}
