// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

public interface IAsyncTestTarget
{
    public Task TestTaskAsync();

    public Task TestTaskAsync(CancellationToken cancellationToken);

    public ValueTask TestValueTaskAsync();
}
