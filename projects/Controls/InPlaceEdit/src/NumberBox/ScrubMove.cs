// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>The outcome of one pointer move during a scrub gesture.</summary>
/// <param name="Started">Whether this move started the scrub.</param>
/// <param name="Steps">The signed whole steps travelled; zero when nothing changes.</param>
internal readonly record struct ScrubMove(bool Started, int Steps);
