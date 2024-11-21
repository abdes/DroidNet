// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Media;

namespace DroidNet.Tests;

/// <summary>
/// Provides helpers for the <see cref="CompositionTarget"/> class.
/// </summary>
internal static class CompositionTargetHelper
{
    /// <summary>
    /// Provides a method to execute code after the rendering pass is completed.
    /// <seealso href="https://github.com/CommunityToolkit/Tooling-Windows-Submodule/blob/main/CommunityToolkit.Tests.Shared/VisualUITestBase.cs"/>
    /// <seealso href="https://github.com/microsoft/microsoft-ui-xaml/blob/c045cde57c5c754683d674634a0baccda34d58c4/dev/dll/SharedHelpers.cpp#L399"/>
    /// <seealso href="https://devblogs.microsoft.com/premier-developer/the-danger-of-taskcompletionsourcet-class/"/>
    /// </summary>
    /// <param name="action">Action to be executed after render pass.</param>
    /// <param name="options"><see cref="TaskCreationOptions"/> for how to handle async calls with <see cref="TaskCompletionSource{TResult}"/>.</param>
    /// <returns>Awaitable Task.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "we propagate all exceptions through `taskCompletionSource.SetException`")]
    public static Task<bool> ExecuteAfterCompositionRenderingAsync(Action action, TaskCreationOptions? options = null)
    {
        var taskCompletionSource = options.HasValue ? new TaskCompletionSource<bool>(options.Value)
            : new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

        try
        {
            void Callback(object? sender, object args)
            {
                _ = sender; // Unused
                _ = args; // Unused

                CompositionTarget.Rendering -= Callback;

                action();

                taskCompletionSource.SetResult(true);
            }

            CompositionTarget.Rendering += Callback;
        }
        catch (Exception ex)
        {
            taskCompletionSource.SetException(ex); // Note this can just sometimes be a wrong thread exception, see WinUI function notes.
        }

        return taskCompletionSource.Task;
    }
}
