// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Windows;

/// <summary>
///     A participant's part of one window's close, which runs only when every participant prepared the close.
/// </summary>
/// <remarks>
///     The commits run in level order once every participant has prepared; a commit that refuses keeps the window open.
///     The finalizers run in the same order once every commit succeeded, before the window is destroyed, and cannot keep
///     it open. Disposing the step releases what its preparation holds, whether or not the close happened.
/// </remarks>
/// <param name="commit">Commits the participant's part; <see langword="false" /> keeps the window open.</param>
/// <param name="finalize">Runs after every part committed.</param>
/// <param name="prepared">What the preparation holds until the close ends.</param>
public sealed class WindowCloseStep(Func<Task<bool>>? commit = null, Func<Task>? finalize = null, IDisposable? prepared = null)
    : IDisposable
{
    /// <summary>Gets a step that has nothing to do for the close.</summary>
    public static WindowCloseStep None { get; } = new();

    /// <summary>Commits the participant's part of the close.</summary>
    /// <returns><see langword="true" /> when the close may go on; <see langword="false" /> keeps the window open.</returns>
    public Task<bool> CommitAsync() => commit?.Invoke() ?? Task.FromResult(true);

    /// <summary>Finishes the participant's part of the close, once every part committed.</summary>
    /// <returns>A task that completes when the part is finished.</returns>
    public Task FinalizeAsync() => finalize?.Invoke() ?? Task.CompletedTask;

    /// <inheritdoc />
    public void Dispose() => prepared?.Dispose();
}
