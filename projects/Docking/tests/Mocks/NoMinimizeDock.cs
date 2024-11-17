// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

namespace DroidNet.Docking.Tests.Mocks;

/// <summary>
/// A test dock class that cannot be minimized.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class NoMinimizeDock : Dock
{
    /// <inheritdoc/>
    public override bool CanMinimize => false;
}
