// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

public abstract class TargetedChange<TTarget>(TTarget target) : Change
{
    public TTarget Target { get; } = target;
}
