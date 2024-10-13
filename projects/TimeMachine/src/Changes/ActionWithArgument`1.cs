// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

public class ActionWithArgument<TArgument>(Action<TArgument?> action, TArgument? argument) : Change
{
    public override void Apply() => action(argument);
}
