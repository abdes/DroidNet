// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections.ObjectModel;

public interface IDockTray
{
    ReadOnlyObservableCollection<IDock> MinimizedDocks { get; }

    bool HasMinimizedDocks { get; }
}
