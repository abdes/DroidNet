// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

public interface ILayoutSegment
{
    DockGroupOrientation Orientation { get; }

    bool StretchToFill { get; }

    IDocker Docker { get; }
}
