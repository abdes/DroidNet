// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

public interface ILayoutSegment
{
    public DockGroupOrientation Orientation { get; }

    public bool StretchToFill { get; }

    public IDocker Docker { get; }
}
