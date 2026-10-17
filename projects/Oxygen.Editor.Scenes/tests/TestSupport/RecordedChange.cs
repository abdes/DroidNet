// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Changes;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>One recorded <c>Changed</c> event.</summary>
/// <param name="Args">The event's arguments.</param>
/// <param name="ThreadId">The managed id of the thread that raised it.</param>
public sealed record RecordedChange(SceneChangedEventArgs Args, int ThreadId)
{
    /// <summary>Gets the event's changes.</summary>
    public IReadOnlyList<SceneChange> Changes => this.Args.Changes;
}
