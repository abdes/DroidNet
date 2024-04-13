// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Collections.ObjectModel;

public interface IDockGroup : ILayoutSegment
{
    public ReadOnlyObservableCollection<IDock> Docks { get; }
}
