// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Applies a confirmed content-source configuration through the owning workspace.</summary>
/// <param name="expected">The project configuration shown when the change began.</param>
/// <param name="candidate">The complete candidate manifest, including source priority.</param>
public sealed class ChangeContentMountsRequestMessage(ProjectContext expected, ProjectInfo candidate) : AsyncRequestMessage<bool>
{
    /// <summary>Gets the accepted project configuration before the change.</summary>
    public ProjectContext Expected { get; } = expected;

    /// <summary>Gets the candidate manifest.</summary>
    public ProjectInfo Candidate { get; } = candidate;
}
