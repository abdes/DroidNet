// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Keeps content failures visible independently of which output panel is selected.</summary>
public partial class WorkspaceViewModel
{
    private bool hasContentFailure;
    private string contentFailureTitle = string.Empty;
    private string contentFailureMessage = string.Empty;

    /// <summary>Gets or sets a value indicating whether the workspace has an unresolved content failure.</summary>
    public bool HasContentFailure
    {
        get => this.hasContentFailure;
        set => this.SetProperty(ref this.hasContentFailure, value);
    }

    /// <summary>Gets or sets the failed workspace capability.</summary>
    public string ContentFailureTitle
    {
        get => this.contentFailureTitle;
        set => this.SetProperty(ref this.contentFailureTitle, value);
    }

    /// <summary>Gets or sets the failure summary displayed beside recovery actions.</summary>
    public string ContentFailureMessage
    {
        get => this.contentFailureMessage;
        set => this.SetProperty(ref this.contentFailureMessage, value);
    }

    [RelayCommand]
    private Task RetryContentAsync() => this.RefreshCookedRootsAsync();

    [RelayCommand]
    private void ShowContentFailureDetails()
    {
        if (Dockable.FromId("log") is { } output)
        {
            this.RevealTool(output);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A secondary suspension failure must remain visible without escaping the refresh recovery command.")]
    private async Task SuspendUnavailablePreviewAsync(ProjectContext project)
    {
        try
        {
            await this.engineService.SuspendCookedContentAsync().ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.LogCookedContentSuspensionFailed(failure, project.ProjectRoot);
            this.ContentFailureMessage += " Preview suspension also failed; see Details.";
            RuntimeOperationResults.PublishFailure(
                this.operationResults,
                this.statusReducer,
                RuntimeOperationKinds.CookedRootRefresh,
                FailureDomain.AssetMount,
                AssetMountDiagnosticCodes.RefreshFailed,
                "Preview suspension failed",
                "The preview could not be suspended after content refresh failed.",
                this.CreateProjectScope(),
                project.ProjectRoot,
                failure,
                failure.ToString());
        }
    }
}
