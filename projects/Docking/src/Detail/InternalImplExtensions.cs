// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

internal static class InternalImplExtensions
{
    internal static Dock AsDock(this IDock dock)
        => dock as Dock ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(Dock)}`, but got an object of type `{dock.GetType()}`",
            nameof(dock));

    internal static DockGroup AsDockGroup(this IDockGroup group)
        => group as DockGroup ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(DockGroup)}`, but got an object of type `{group.GetType()}`",
            nameof(group));

    internal static Docker AsDocker(this IDocker docker)
        => docker as Docker ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(Docker)}`, but got an object of type `{docker.GetType()}`",
            nameof(docker));
}
