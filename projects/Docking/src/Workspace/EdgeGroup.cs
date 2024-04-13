// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using DroidNet.Docking;

internal sealed class EdgeGroup(IDocker docker, DockGroupOrientation orientation) : LayoutGroup(docker, orientation)
{
    public override DockGroupOrientation Orientation
    {
        get => base.Orientation;
        internal set => throw new InvalidOperationException(
            $"orientation of an {nameof(EdgeGroup)} can only be set at creation");
    }
}
