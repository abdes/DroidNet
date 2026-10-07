// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Tests;

/// <summary>
/// Provides helpers for the <see cref="CompositionTarget"/> class.
/// </summary>
internal static class CompositionTargetHelper
{
    /// <summary>The longest wait for a frame before the test fails instead of blocking the run.</summary>
    private static readonly TimeSpan FrameTimeout = TimeSpan.FromSeconds(10);

    /// <summary>
    /// Provides a method to execute code after the rendering pass is completed.
    /// <seealso href="https://github.com/CommunityToolkit/Tooling-Windows-Submodule/blob/main/CommunityToolkit.Tests.Shared/VisualUITestBase.cs"/>
    /// <seealso href="https://github.com/microsoft/microsoft-ui-xaml/blob/c045cde57c5c754683d674634a0baccda34d58c4/dev/dll/SharedHelpers.cpp#L399"/>
    /// <seealso href="https://devblogs.microsoft.com/premier-developer/the-danger-of-taskcompletionsourcet-class/"/>
    /// </summary>
    /// <param name="action">Action to be executed after render pass.</param>
    /// <param name="options"><see cref="TaskCreationOptions"/> for how to handle async calls with <see cref="TaskCompletionSource{TResult}"/>.</param>
    /// <returns>Awaitable Task.</returns>
    /// <remarks>
    /// Frames are produced only while the test window renders; a minimized or occluded window produces none.
    /// The wait therefore fails with <see cref="TimeoutException"/> after <see cref="FrameTimeout"/> rather than
    /// blocking every later test. Must be called on the UI thread.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "we propagate all exceptions through `taskCompletionSource.SetException`")]
    public static Task<bool> ExecuteAfterCompositionRenderingAsync(Action action, TaskCreationOptions? options = null)
    {
        var taskCompletionSource = options.HasValue ? new TaskCompletionSource<bool>(options.Value)
            : new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

        try
        {
            var timer = DispatcherQueue.GetForCurrentThread().CreateTimer();
            timer.Interval = FrameTimeout;
            timer.IsRepeating = false;

            void Callback(object? sender, object args)
            {
                _ = sender; // Unused
                _ = args; // Unused

                CompositionTarget.Rendering -= Callback;
                timer.Stop();

                try
                {
                    action();
                    _ = taskCompletionSource.TrySetResult(true);
                }
                catch (Exception ex)
                {
                    _ = taskCompletionSource.TrySetException(ex);
                }
            }

            void OnTimeout(DispatcherQueueTimer sender, object args)
            {
                _ = args; // Unused

                sender.Stop();
                CompositionTarget.Rendering -= Callback;
                _ = taskCompletionSource.TrySetException(new TimeoutException(
                    $"No frame was rendered within {FrameTimeout.TotalSeconds:0} s; the test window is not rendering (minimized or occluded). Wait on layout or the dispatcher instead of a frame."));
            }

            timer.Tick += OnTimeout;
            CompositionTarget.Rendering += Callback;
            timer.Start();
        }
        catch (Exception ex)
        {
            _ = taskCompletionSource.TrySetException(ex); // Note this can just sometimes be a wrong thread exception, see WinUI PropertyNames notes.
        }

        return taskCompletionSource.Task;
    }
}
