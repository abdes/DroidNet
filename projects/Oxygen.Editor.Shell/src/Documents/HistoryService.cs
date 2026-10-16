// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>Holds a session's global history and drops its entries when the session ends.</summary>
public sealed class HistoryService : IHistoryService, IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="HistoryService"/> class.</summary>
    public HistoryService()
    {
        this.GlobalHistory = new HistoryKeeper(this);
    }

    /// <inheritdoc />
    public HistoryKeeper GlobalHistory { get; }

    /// <inheritdoc />
    public void Dispose() => this.GlobalHistory.Clear();
}
