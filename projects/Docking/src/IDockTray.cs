// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections.ObjectModel;

public interface IDockTray
{
    public ReadOnlyObservableCollection<IDock> Docks { get; }

    IDocker Docker { get; }

    bool IsEmpty { get; }

    bool IsVertical { get; }
}
