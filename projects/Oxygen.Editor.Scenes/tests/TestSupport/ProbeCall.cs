// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>One call to a <see cref="ProbeEdit"/>.</summary>
/// <param name="Label">The label of the probe that was called.</param>
/// <param name="Step">The step.</param>
/// <param name="ThreadId">The managed id of the thread that made the call.</param>
public sealed record ProbeCall(string Label, ProbeStep Step, int ThreadId);
