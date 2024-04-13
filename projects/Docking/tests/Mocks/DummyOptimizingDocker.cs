// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Workspace;

[ExcludeFromCodeCoverage]
public class DummyOptimizingDocker : DummyDocker, IOptimizingDocker
{
    public bool ConsolidateUpCalled { get; private set; }

    public void ConsolidateUp(IDockGroup startingGroup) => this.ConsolidateUpCalled = true;

    public void Reset() => this.ConsolidateUpCalled = false;
}
