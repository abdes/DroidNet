// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Messages;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Hands confirmed content-mount changes to the project preview, which commits them while no cook publishes.</summary>
public partial class WorkspaceViewModel
{
    private Task<bool> ChangeContentMountsAsync(ChangeContentMountsRequestMessage message)
        => this.previewSession is { } session
            ? session.ChangeMountsAsync(message.Expected, message.Candidate)
            : Task.FromResult(false);
}
