// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The ordered record of calls to probes that share it.
/// </summary>
public sealed class ProbeLog
{
    private readonly Lock gate = new();
    private readonly List<ProbeCall> calls = [];

    /// <summary>Gets the calls, in order.</summary>
    public IReadOnlyList<ProbeCall> Calls
    {
        get
        {
            lock (this.gate)
            {
                return [.. this.calls];
            }
        }
    }

    /// <summary>Gets the calls as <c>"label:step"</c>, in order, for compact assertions.</summary>
    public IReadOnlyList<string> Steps => [.. this.Calls.Select(static call => $"{call.Label}:{call.Step}")];

    internal void Add(string label, ProbeStep step)
    {
        lock (this.gate)
        {
            this.calls.Add(new ProbeCall(label, step, Environment.CurrentManagedThreadId));
        }
    }
}
