// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

internal interface IOptimizingDocker
{
    void ConsolidateUp(IDockGroup startingGroup);
}
