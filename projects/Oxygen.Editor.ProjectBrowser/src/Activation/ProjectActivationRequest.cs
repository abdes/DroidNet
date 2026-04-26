// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.ProjectBrowser.Activation;

/// <summary>
/// Request to open or create a project and activate the workspace.
/// </summary>
public sealed record ProjectActivationRequest
{
    /// <summary>
    /// Gets the activation correlation identity.
    /// </summary>
    public Guid CorrelationId { get; init; } = Guid.NewGuid();

    /// <summary>
    /// Gets the request timestamp.
    /// </summary>
    public DateTimeOffset RequestedAt { get; init; } = DateTimeOffset.Now;

    /// <summary>
    /// Gets the activation mode.
    /// </summary>
    public required ProjectActivationMode Mode { get; init; }

    /// <summary>
    /// Gets the Project Browser surface that initiated the request.
    /// </summary>
    public required ProjectActivationSourceSurface SourceSurface { get; init; }

    /// <summary>
    /// Gets the project folder path for open-existing requests.
    /// </summary>
    public string? ProjectLocation { get; init; }

    /// <summary>
    /// Gets the persistent recent-project record identity, when available.
    /// </summary>
    public string? RecentEntryId { get; init; }

    /// <summary>
    /// Gets the template root folder for create-from-template requests.
    /// </summary>
    public string? TemplateLocation { get; init; }

    /// <summary>
    /// Gets the template display identity for diagnostics and usage updates.
    /// </summary>
    public string? TemplateId { get; init; }

    /// <summary>
    /// Gets the new project name for create-from-template requests.
    /// </summary>
    public string? ProjectName { get; init; }

    /// <summary>
    /// Gets the parent folder where the new project folder will be created.
    /// </summary>
    public string? ParentLocation { get; init; }

    /// <summary>
    /// Gets the fallback category for create-from-template requests.
    /// </summary>
    public Category? Category { get; init; }

    /// <summary>
    /// Gets the fallback thumbnail for create-from-template requests.
    /// </summary>
    public string? Thumbnail { get; init; }
}
