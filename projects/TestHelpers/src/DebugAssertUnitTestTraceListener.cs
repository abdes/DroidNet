// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace DroidNet.TestHelpers;

/// <summary>
/// TraceListener used for trapping assertion failures during unit tests.
/// </summary>
[ExcludeFromCodeCoverage]
public class DebugAssertUnitTestTraceListener : TraceListener
{
    private readonly List<string> writes = [];

    /// <summary>Gets the list of recorded assertion messages.</summary>
    /// <value>The list of recorded assertion messages.</value>
    public IEnumerable<string> RecordedMessages => this.writes.AsReadOnly();

    /// <summary>Write the message to the list of recorded messages.</summary>
    /// <param name="message">The assertion message.</param>
    public override void Write(string? message)
    {
        if (message != null)
        {
            this.writes.Add(message);
        }
    }

    /// <summary>Write the message to the list of recorded messages.</summary>
    /// <param name="message">The assertion message.</param>
    public override void WriteLine(string? message) => this.Write(message);

    /// <summary>Clear any saved assertion messages.</summary>
    public void Clear() => this.writes.Clear();
}
