// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

namespace DroidNet.Docking.Tests.Mocks;

/// <summary>
/// A test dock suitable for the center dock group.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class SimpleCenterDock : Dock
{
    /// <inheritdoc/>
    public override bool CanMinimize => false;

    /// <inheritdoc/>
    public override bool CanClose => false;
}
