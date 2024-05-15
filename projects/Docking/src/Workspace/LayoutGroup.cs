// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

internal class LayoutGroup(IDocker docker, DockGroupOrientation orientation = DockGroupOrientation.Undetermined)
    : LayoutSegment(docker, orientation)
{
    public override string ToString() => $"{base.ToString()}";
}
