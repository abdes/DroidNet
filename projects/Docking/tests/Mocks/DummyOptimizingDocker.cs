// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Tests.Mocks;

/// <summary>
/// A dummy <see cref="Docker" /> that supports optimization.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class DummyOptimizingDocker : DummyDocker, IOptimizingDocker
{
    public bool ConsolidateUpCalled { get; private set; }

    /// <inheritdoc/>
    public void ConsolidateUp(IDockGroup startingGroup) => this.ConsolidateUpCalled = true;

    public void Reset() => this.ConsolidateUpCalled = false;
}
