// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Commits confirmed content-mount changes while excluding competing cook publication.</summary>
public partial class WorkspaceViewModel
{
    private async Task<bool> ChangeContentMountsAsync(ChangeContentMountsRequestMessage message)
    {
        var service = new Oxygen.Editor.World.Services.ContentMountChangeService(
            this.container.Resolve<IContentCookCoordinator>(),
            this.projectContextService,
            this.projectManager,
            this.engineService,
            this.container.Resolve<CookedContentMountService>(),
            this.cookedCatalog!,
            this.cookHosting!);
        await service.ApplyAsync(
            message.Expected,
            message.Candidate,
            next =>
            {
                this.publicationRegistration?.Dispose();
                this.publicationRegistration = this.container.Resolve<CookPublicationService>().RegisterPreview(next, () => this.CreatePublicationPreviewAsync(next));
                this.UpdateLoadedProjectMounts(message.Candidate);
            },
            CancellationToken.None).ConfigureAwait(true);
        return true;
    }

    private void UpdateLoadedProjectMounts(ProjectInfo candidate)
    {
        if (this.projectManager.CurrentProject?.ProjectInfo is { } current && current.Id == candidate.Id)
        {
            current.AuthoringMounts.Clear();
            foreach (var mount in candidate.AuthoringMounts)
            {
                current.AuthoringMounts.Add(mount);
            }

            current.LocalFolderMounts.Clear();
            foreach (var mount in candidate.LocalFolderMounts)
            {
                current.LocalFolderMounts.Add(mount);
            }

            current.CookedContentOrder.Clear();
            foreach (var source in candidate.CookedContentOrder)
            {
                current.CookedContentOrder.Add(source);
            }
        }
    }
}
