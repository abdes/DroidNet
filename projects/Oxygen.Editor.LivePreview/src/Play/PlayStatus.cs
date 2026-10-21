// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>A snapshot of the workspace's Play state.</summary>
/// <param name="Activity">What Play is doing.</param>
/// <param name="DocumentId">The scene document being prepared, played or that failed; null when none.</param>
/// <param name="SceneName">The name of that scene, or an empty string.</param>
/// <param name="IsStale">Whether a document changed since the running session started.</param>
/// <param name="Failure">Why the last Play failed, when <paramref name="Activity"/> is <see cref="PlayActivity.Failed"/>.</param>
/// <param name="FramesPerSecond">The running player's frame rate, once it reported one.</param>
public sealed record PlayStatus(
    PlayActivity Activity,
    DocumentId? DocumentId = null,
    string SceneName = "",
    bool IsStale = false,
    PlayFailure? Failure = null,
    int? FramesPerSecond = null)
{
    /// <summary>Gets the status of a workspace where nothing plays.</summary>
    public static PlayStatus Idle { get; } = new(PlayActivity.Idle);

    /// <summary>Gets a value indicating whether a Play is on its way to a session.</summary>
    public bool IsBusy => this.Activity is PlayActivity.Cooking or PlayActivity.StartingPlayer or PlayActivity.LoadingScene;

    /// <summary>Gets a value indicating whether a session runs.</summary>
    public bool IsRunning => this.Activity is PlayActivity.Playing or PlayActivity.Paused;
}
