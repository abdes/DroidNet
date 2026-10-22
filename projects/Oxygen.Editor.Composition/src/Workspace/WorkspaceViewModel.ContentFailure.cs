// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking;
using Oxygen.Editor.LivePreview.Session;

namespace Oxygen.Editor.Workspace;

/// <summary>Shows why the project preview is unavailable, independently of which output panel is selected.</summary>
public partial class WorkspaceViewModel
{
    /// <summary>Gets a value indicating whether the engine or the project's content is unavailable.</summary>
    public bool HasContentFailure
        => this.previewSession?.State is PreviewSessionState.EngineUnavailable or PreviewSessionState.ContentUnavailable;

    /// <summary>Gets the failed capability.</summary>
    public string ContentFailureTitle => this.previewSession?.Failure?.Title ?? string.Empty;

    /// <summary>Gets the failure summary displayed beside the recovery actions.</summary>
    public string ContentFailureMessage => this.previewSession?.Failure?.Message ?? string.Empty;

    [RelayCommand]
    private async Task RetryContentAsync()
    {
        if (this.previewSession is { } session)
        {
            _ = await session.RetryAsync().ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private void ShowContentFailureDetails()
    {
        if (this.Docker?.FindDockable("log") is { } output)
        {
            this.RevealTool(output);
        }
    }

    private void ObservePreviewSession()
    {
        if (this.previewSession is { } session)
        {
            session.PropertyChanged += this.OnPreviewSessionChanged;
        }
    }

    private void UnobservePreviewSession()
    {
        if (this.previewSession is { } session)
        {
            session.PropertyChanged -= this.OnPreviewSessionChanged;
        }
    }

    private void OnPreviewSessionChanged(object? sender, PropertyChangedEventArgs args)
    {
        this.OnPropertyChanged(nameof(this.HasContentFailure));
        this.OnPropertyChanged(nameof(this.ContentFailureTitle));
        this.OnPropertyChanged(nameof(this.ContentFailureMessage));
    }
}
