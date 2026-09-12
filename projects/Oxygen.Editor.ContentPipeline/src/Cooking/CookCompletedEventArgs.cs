// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Refreshes current consumers before the shared cook writer is released.</summary>
/// <param name="args">The completed native output and its project lifetime.</param>
/// <returns>Completion of the consumer refresh.</returns>
public delegate Task CookCompletedHandler(CookCompletedEventArgs args);

/// <summary>Identifies completed derived output independently of the UI that requested it.</summary>
/// <param name="project">The project lifetime that owned the cook.</param>
/// <param name="result">The completed pipeline result.</param>
public sealed class CookCompletedEventArgs(ProjectContext project, ContentCookResult result) : EventArgs
{
    /// <summary>Gets the originating project lifetime.</summary>
    public ProjectContext Project { get; } = project;

    /// <summary>Gets the completed pipeline result.</summary>
    public ContentCookResult Result { get; } = result;
}
