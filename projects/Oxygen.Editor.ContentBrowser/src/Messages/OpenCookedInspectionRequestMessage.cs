// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Opens an explicit read-only inspection in the originating workspace's document host.</summary>
/// <param name="project">The originating project activation.</param>
/// <param name="scopeUri">The requested asset/folder scope, or null for project output.</param>
/// <param name="validate">Whether the request also checks output integrity.</param>
public sealed class OpenCookedInspectionRequestMessage(ProjectContext project, Uri? scopeUri, bool validate) : AsyncRequestMessage<bool>
{
    /// <summary>Gets the originating project.</summary>
    public ProjectContext Project { get; } = project;

    /// <summary>Gets the requested scope.</summary>
    public Uri? ScopeUri { get; } = scopeUri;

    /// <summary>Gets a value indicating whether root integrity should be checked.</summary>
    public bool Validate { get; } = validate;
}
