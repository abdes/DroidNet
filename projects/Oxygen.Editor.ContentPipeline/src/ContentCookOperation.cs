// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Identifies a cook request and the project lifetime that owns it.</summary>
/// <param name="OperationId">The request identity, assigned before it queues.</param>
/// <param name="Project">The captured active project context.</param>
/// <param name="ProjectLifetime">The coordinator's activation generation.</param>
public sealed record ContentCookOperation(Guid OperationId, ProjectContext Project, long ProjectLifetime)
{
    /// <summary>
    /// Gets a value indicating whether the operation may replace cooked content published in a format this editor does
    /// not read: a cook of the whole project the user asked for, which publishes everything again. Automatic cooks,
    /// preview demands and narrower cooks leave such a publication alone.
    /// </summary>
    public bool UpgradesPublication { get; init; }
}
