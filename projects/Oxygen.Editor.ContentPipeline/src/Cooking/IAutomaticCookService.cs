// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Schedules acknowledged saved sources without delaying their document's Save result.</summary>
public interface IAutomaticCookService
{
    /// <summary>Queues a changed saved source, or resumes a cook waiting for that source.</summary>
    /// <param name="sourcePath">The acknowledged source's absolute path.</param>
    /// <param name="contentHash">The acknowledged saved bytes' SHA-256 hash.</param>
    /// <param name="contentChanged">Whether this save changed the persisted source bytes.</param>
    public void NotifySaved(string sourcePath, string contentHash, bool contentChanged = true);
}
