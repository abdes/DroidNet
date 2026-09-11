// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Correlates nested workflow messages without associating unrelated concurrent work.</summary>
internal static partial class CookRunContext
{
    private static readonly AsyncLocal<IProgress<CookRunProgress>?> Active = new();

    /// <summary>Gets the current workflow's message sink.</summary>
    internal static IProgress<CookRunProgress>? Current => Active.Value;

    /// <summary>Reports only to the current asynchronous cook call chain.</summary>
    /// <param name="progress">The phase, message, or asset change.</param>
    internal static void Report(CookRunProgress progress) => Active.Value?.Report(progress);

    /// <summary>Enters a scope whose prior context is restored on disposal.</summary>
    /// <param name="progress">The owning run's synchronous message sink.</param>
    /// <returns>The context lifetime.</returns>
    internal static IDisposable Enter(IProgress<CookRunProgress>? progress)
    {
        var previous = Active.Value;
        Active.Value = progress;
        return new Scope(previous);
    }

    private sealed partial class Scope(IProgress<CookRunProgress>? previous) : IDisposable
    {
        public void Dispose() => Active.Value = previous;
    }
}
