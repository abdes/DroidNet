// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections.ObjectModel;

public interface IDockTray
{
    public ReadOnlyObservableCollection<IDock> MinimizedDocks { get; }

    bool IsEmpty { get; }
}
