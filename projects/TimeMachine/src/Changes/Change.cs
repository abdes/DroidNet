// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

public abstract class Change : IChange
{
    public required object Key { get; init; }

    public abstract void Apply();
}
