// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using DroidNet.Docking.Detail;

public class DummyOptimizingDocker : DummyDocker, IOptimizingDocker
{
    public bool ConsolidateUpCalled { get; set; }

    public void ConsolidateUp(IDockGroup startingGroup) => this.ConsolidateUpCalled = true;

    public void Reset() => this.ConsolidateUpCalled = false;
}
