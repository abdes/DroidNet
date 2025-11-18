// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Oxygen.Editor.Projects;
using Windows.Storage;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Represents a project item with a thumbnail image.
/// </summary>
internal sealed partial class ProjectItemWithThumbnail : ObservableObject
{
    /// <summary>
    /// Gets the project information.
    /// </summary>
    public required IProjectInfo ProjectInfo { get; init; }

    /// <summary>
    /// Gets local path or ms-appx URI for the thumbnail. This may be null when using a default thumbnail.
    /// </summary>
    public string? ThumbnailPath { get; init; }

    /// <summary>
    /// Gets the loaded thumbnail image source. This property is updated asynchronously.
    /// </summary>
    [ObservableProperty]
    public partial ImageSource? Thumbnail { get; set; }

    /// <summary>
    /// Create a project item and start asynchronous thumbnail loading. The loaded image will be
    /// assigned to <see cref="Thumbnail"/> when completed.
    /// </summary>
    /// <param name="projectInfo">The project info.</param>
    /// <param name="defaultThumbnailPath">A default image URI to use when no thumbnail exists.</param>
    /// <returns>A new <see cref="ProjectItemWithThumbnail"/> instance with thumbnail loading started.</returns>
    public static ProjectItemWithThumbnail Create(IProjectInfo projectInfo, string defaultThumbnailPath)
    {
        var item = new ProjectItemWithThumbnail
        {
            ProjectInfo = projectInfo,
            ThumbnailPath = projectInfo.Thumbnail is null
                ? null
                : Path.GetFullPath(Path.Combine(projectInfo.Location ?? string.Empty, projectInfo.Thumbnail ?? string.Empty)),
        };
        _ = item.LoadThumbnailAsync(defaultThumbnailPath);
        return item;
    }

    private static string PathToFileUri(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return path;
        }

        // Normalize to full path
        var absolute = Path.GetFullPath(path);

        // Convert backslashes to slashes and prefix with file:/// scheme
        var escaped = absolute.Replace('\\', '/');
        if (!escaped.StartsWith('/'))
        {
            // absolute windows path -> e.g. C:/folder/file
            escaped = "/" + escaped;
        }

        return "file://" + escaped;
    }

    private async Task LoadThumbnailAsync(string defaultThumbnail)
    {
        var path = this.ThumbnailPath;
        if (!string.IsNullOrEmpty(path) && File.Exists(path))
        {
            if (await this.TryLoadFromPathAsync(path).ConfigureAwait(false))
            {
                return;
            }
        }

        // Fallback to default
        this.SetDefaultThumbnail(defaultThumbnail);
    }

    private void SetDefaultThumbnail(string defaultThumbnail)
    {
        try
        {
            var bmp = new BitmapImage(new Uri(defaultThumbnail));
            this.Thumbnail = bmp;
            Debug.WriteLine($"[ProjectItemWithThumbnail] SetDefaultThumbnail applied for Project={this.ProjectInfo?.Name}, DefaultUri={defaultThumbnail}");
        }
        catch (UriFormatException)
        {
            this.Thumbnail = null;
        }
    }

    private async Task<bool> TryLoadFromPathAsync(string path)
    {
        try
        {
            StorageFile? file;
            if (Uri.TryCreate(path, UriKind.Absolute, out var uri) && string.Equals(uri.Scheme, "ms-appx", StringComparison.OrdinalIgnoreCase))
            {
                file = await StorageFile.GetFileFromApplicationUriAsync(new Uri(path));
            }
            else
            {
                var absolute = path;
                if (absolute.StartsWith("file:/", StringComparison.OrdinalIgnoreCase) || absolute.StartsWith("file:///", StringComparison.OrdinalIgnoreCase))
                {
                    absolute = new Uri(absolute).LocalPath;
                }

                file = await StorageFile.GetFileFromPathAsync(absolute);
            }

            var randomAccessStream = await file.OpenReadAsync();
            Debug.WriteLine($"[ProjectItemWithThumbnail] Opened stream for file {file?.Path} for project {this.ProjectInfo?.Name}");

            var bmp = new BitmapImage();

            // Prefer to set the UriSource on the BitmapImage. This avoids dispatcher marshalling
            // and simplifies runtime behavior when using binding in XAML.
            var pathStr = file?.Path ?? path;
            Debug.WriteLine($"[ProjectItemWithThumbnail] Using UriSource for Project={this.ProjectInfo?.Name}, Uri=" + (pathStr ?? "(null)"));
            if (!string.IsNullOrEmpty(pathStr))
            {
                try
                {
                    // If path is a ms-appx: URI, use it directly; otherwise convert local file path to a file URI.
                    if (Uri.TryCreate(pathStr, UriKind.Absolute, out var uri2) && string.Equals(uri2.Scheme, "ms-appx", StringComparison.OrdinalIgnoreCase))
                    {
                        bmp.UriSource = uri2;
                    }
                    else
                    {
                        bmp.UriSource = new Uri(PathToFileUri(pathStr));
                    }

                    this.Thumbnail = bmp;
                    return true;
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
                {
                    Debug.WriteLine($"[ProjectItemWithThumbnail] Failed to create Uri for path {pathStr}: {ex.Message}");
                }
#pragma warning restore CA1031 // Do not catch general exception types
            }

            return false;
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or FileNotFoundException or ArgumentException or IOException)
        {
            Debug.WriteLine($"[ProjectItemWithThumbnail] TryLoadFromPathAsync failed with {ex.GetType().Name}: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Compares project items by name.
    /// </summary>
    public sealed class ByNameComparer : IComparer<ProjectItemWithThumbnail>, IComparer
    {
        /// <inheritdoc/>
        public int Compare(ProjectItemWithThumbnail? x, ProjectItemWithThumbnail? y)
            => ReferenceEquals(x, y)
                ? 0
                : x is null
                    ? -1
                    : y is null
                        ? 1
                        : string.CompareOrdinal(x.ProjectInfo.Name, y.ProjectInfo.Name);

        /// <inheritdoc />
        int IComparer.Compare(object? x, object? y) => this.Compare(x as ProjectItemWithThumbnail, y as ProjectItemWithThumbnail);
    }

    /// <summary>
    /// Compares project items by last used date.
    /// </summary>
    public sealed class ByLastUsedOnComparer : IComparer<ProjectItemWithThumbnail>, IComparer
    {
        /// <inheritdoc/>
        public int Compare(ProjectItemWithThumbnail? x, ProjectItemWithThumbnail? y)
            => ReferenceEquals(x, y)
                ? 0
                : x is null
                    ? -1
                    : y is null
                        ? 1
                        : DateTime.Compare(x.ProjectInfo.LastUsedOn, y.ProjectInfo.LastUsedOn);

        /// <inheritdoc />
        int IComparer.Compare(object? x, object? y) => this.Compare(x as ProjectItemWithThumbnail, y as ProjectItemWithThumbnail);
    }
}
