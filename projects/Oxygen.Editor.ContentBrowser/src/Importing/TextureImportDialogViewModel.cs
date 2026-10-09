// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Importing;

/// <summary>Reviews a standalone image's name, destination and native texture settings.</summary>
public sealed partial class TextureImportDialogViewModel : ObservableObject
{
    private const string Texture2DShape = "2D";
    private const string CubeShape = "Cube";
    private const string AutoLayout = "Auto";

    private static readonly IReadOnlyDictionary<CubeLayout, string> LayoutNames = new Dictionary<CubeLayout, string>
    {
        [Oxygen.Editor.ContentPipeline.Import.CubeLayout.Panorama] = "Panorama 2:1",
        [Oxygen.Editor.ContentPipeline.Import.CubeLayout.HorizontalStrip] = "Horizontal strip 6:1",
        [Oxygen.Editor.ContentPipeline.Import.CubeLayout.VerticalStrip] = "Vertical strip 1:6",
        [Oxygen.Editor.ContentPipeline.Import.CubeLayout.HorizontalCross] = "Horizontal cross 4:3",
        [Oxygen.Editor.ContentPipeline.Import.CubeLayout.VerticalCross] = "Vertical cross 3:4",
        [Oxygen.Editor.ContentPipeline.Import.CubeLayout.SixFaces] = "Six face files",
    };

    private readonly ProjectContext project;
    private readonly IDialogService dialogs;
    private readonly IReadOnlyList<string>? sourceFaces;
    private string name = string.Empty;
    private string destinationFolder = string.Empty;
    private string intent = "albedo";
    private string colorSpace = "srgb";
    private string format = "rgba8_srgb";
    private string error = string.Empty;
    private bool canAccept;
    private bool isCube;
    private string cubeLayout = AutoLayout;
    private int faceSize = 1024;

    /// <summary>Initializes a new instance of the <see cref="TextureImportDialogViewModel"/> class for a reviewed image import.</summary>
    /// <param name="project">The active project.</param>
    /// <param name="sourcePath">The selected image.</param>
    /// <param name="destinationFolder">The initial authoring destination.</param>
    /// <param name="dialogs">The folder picker service.</param>
    public TextureImportDialogViewModel(ProjectContext project, string sourcePath, string destinationFolder, IDialogService dialogs)
    {
        this.project = project ?? throw new ArgumentNullException(nameof(project));
        this.dialogs = dialogs ?? throw new ArgumentNullException(nameof(dialogs));
        this.SourcePath = Path.GetFullPath(sourcePath);
        this.SourceDimensions = ImageDimensions.TryRead(this.SourcePath);
        this.sourceFaces = TextureSourceAssetImporter.FindCubeFaces(this.SourcePath);
        var stem = Path.GetFileNameWithoutExtension(sourcePath);
        this.Name = this.sourceFaces is null ? stem : TextureSourceAssetImporter.StripFaceSuffix(stem);
        this.DestinationFolder = destinationFolder;
        this.Revalidate();
    }

    /// <summary>Gets the selected image source.</summary>
    public string SourcePath { get; }

    /// <summary>Gets the native intents available for a standalone image.</summary>
    public IReadOnlyList<string> Intents { get; } = ["albedo", "normal", "roughness", "metallic", "ao", "orm", "emissive", "opacity", "data", "height", "hdr_env", "hdr_probe"];

    /// <summary>Gets the texture shapes the import can produce.</summary>
    public IReadOnlyList<string> Shapes { get; } = [Texture2DShape, CubeShape];

    /// <summary>Gets the cube layouts: automatic detection, then each explicit layout.</summary>
    public IReadOnlyList<string> CubeLayouts { get; } = [AutoLayout, .. LayoutNames.Values];

    /// <summary>Gets the panorama face sizes the cooker accepts.</summary>
    public IReadOnlyList<int> FaceSizes { get; } = [256, 512, 1024, 2048, 4096];

    /// <summary>Gets the source image dimensions, or null when its header could not be read.</summary>
    public (int Width, int Height)? SourceDimensions { get; }

    /// <summary>Gets or sets the shape: a 2D texture, or a cube texture built from this image.</summary>
    public string Shape
    {
        get => this.isCube ? CubeShape : Texture2DShape;
        set
        {
            var cube = string.Equals(value, CubeShape, StringComparison.Ordinal);
            if (cube == this.isCube)
            {
                return;
            }

            this.isCube = cube;
            this.OnPropertyChanged();
            this.OnPropertyChanged(nameof(this.IsCube));

            // Environment cubemaps are linear HDR; 2D textures default to color.
            this.Intent = cube ? "hdr_env" : "albedo";
            this.ColorSpace = cube ? "linear" : "srgb";
            this.Format = cube ? "rgba16f" : "rgba8_srgb";
            this.Revalidate();
        }
    }

    /// <summary>Gets a value indicating whether the import produces a cube texture.</summary>
    public bool IsCube => this.isCube;

    /// <summary>Gets or sets how the image lays out the cube faces.</summary>
    public string SelectedCubeLayout
    {
        get => this.cubeLayout;
        set
        {
            if (this.SetProperty(ref this.cubeLayout, value))
            {
                this.OnPropertyChanged(nameof(this.IsPanorama));
                this.Revalidate();
            }
        }
    }

    /// <summary>Gets the layout the chosen or detected layout resolves to, or null when detection failed.</summary>
    public CubeLayout? ResolvedCubeLayout
        => string.Equals(this.cubeLayout, AutoLayout, StringComparison.Ordinal)
            ? this.sourceFaces is not null ? Oxygen.Editor.ContentPipeline.Import.CubeLayout.SixFaces
            : this.SourceDimensions is { } size ? TextureSourceAssetImporter.DetectCubeLayout(size.Width, size.Height) : null
            : LayoutNames.First(entry => string.Equals(entry.Value, this.cubeLayout, StringComparison.Ordinal)).Key;

    /// <summary>Gets a description of the detected layout for the automatic choice.</summary>
    public string DetectedLayoutText
        => this.sourceFaces is { } faces && this.ResolvedCubeLayout == Oxygen.Editor.ContentPipeline.Import.CubeLayout.SixFaces
            ? "Faces +X, -X, +Y, -Y, +Z, -Z: " + string.Join(", ", faces.Select(Path.GetFileName)) + "."
            : this.SourceDimensions is not { } size ? "Image size unknown; choose the layout."
            : this.ResolvedCubeLayout is { } layout && string.Equals(this.cubeLayout, AutoLayout, StringComparison.Ordinal)
            ? string.Create(CultureInfo.InvariantCulture, $"Detected {LayoutNames[layout]} from {size.Width} × {size.Height}.")
            : string.Create(CultureInfo.InvariantCulture, $"{size.Width} × {size.Height}");

    /// <summary>Gets a value indicating whether the resolved layout is a panorama, which needs a face size.</summary>
    public bool IsPanorama => this.ResolvedCubeLayout == Oxygen.Editor.ContentPipeline.Import.CubeLayout.Panorama;

    /// <summary>Gets or sets the cube face size for a panorama, in pixels.</summary>
    public int FaceSize
    {
        get => this.faceSize;
        set
        {
            if (this.SetProperty(ref this.faceSize, value))
            {
                this.Revalidate();
            }
        }
    }

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

    /// <summary>Gets what the chosen format means for a cube texture, or an empty string.</summary>
    public string FormatNote
        => !this.isCube || this.format is "rgba16f" or "rgba32f" ? string.Empty
            : "An LDR cube can only be displayed: a sky light needs rgba16f or rgba32f to light the scene.";

    /// <summary>Gets the inline validation failure.</summary>
    public string Error
    {
        get => this.error;
        private set => this.SetProperty(ref this.error, value);
    }

    /// <summary>Gets a value indicating whether the reviewed import is valid and collision-free.</summary>
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

    private static Uri ToUri(string folder)
        => new("asset:///" + string.Join('/', folder.Trim().Trim('/').Split('/').Select(Uri.EscapeDataString)));

    [RelayCommand]
    private async Task BrowseDestinationAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FolderPickerSpec("Choose texture destination", "Oxygen.TextureDestination")
            {
                SuggestedStartFolder = this.project.ProjectRoot,
            };
            var selected = await this.dialogs.PickFolderAsync(picker, cancellationToken).ConfigureAwait(true);
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
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.Error = "Could not select the destination folder. " + ex.Message;
            this.CanAccept = false;
        }
    }

    private TextureSourceImportRequest CreateRequest()
        => new(this.project, this.SourcePath, ToUri(this.DestinationFolder), this.Name.Trim(), this.Intent, this.ColorSpace, this.Format)
        {
            Cube = this.isCube && this.ResolvedCubeLayout is { } layout
                ? new TextureCubeImport(layout, layout == Oxygen.Editor.ContentPipeline.Import.CubeLayout.Panorama ? this.faceSize : null)
                : null,
        };

    private void Revalidate()
    {
        this.CanAccept = false;
        this.OnPropertyChanged(nameof(this.DetectedLayoutText));
        this.OnPropertyChanged(nameof(this.IsPanorama));
        if (this.isCube && this.ResolvedCubeLayout is null)
        {
            this.Error = "The image is not a 2:1 panorama, a strip, a cross or one of six face files. Choose its cube layout.";
            return;
        }

        try
        {
            var target = TextureSourceImportTarget.Resolve(this.CreateRequest());
            if (target.ImagePaths.Any(File.Exists) || File.Exists(target.DescriptorPath))
            {
                this.Error = "A texture asset with this name already exists in the destination.";
                this.OnPropertyChanged(nameof(this.OutputVirtualPath));
                return;
            }

            this.Error = string.Empty;
            this.CanAccept = true;
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or UnauthorizedAccessException)
        {
            this.Error = ex.Message;
        }

        this.OnPropertyChanged(nameof(this.OutputVirtualPath));
    }
}
