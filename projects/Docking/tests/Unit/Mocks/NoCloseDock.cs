// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

namespace DroidNet.Docking.Tests.Mocks;

[ExcludeFromCodeCoverage]
internal sealed partial class NoCloseDock : Dock
{
    /// <inheritdoc/>
    public override bool CanClose => false;
}
