// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Transactions;

using DroidNet.TimeMachine.Changes;

public interface ITransaction : IChange, IDisposable
{
    void Commit();

    void Rollback();

    void AddChange(IChange change);
}
