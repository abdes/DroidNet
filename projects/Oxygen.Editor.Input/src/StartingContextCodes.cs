// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Input;

/// <summary>The codes of the problems a scene's starting input context can have, reported by
/// <see cref="StartingContextRules.Check"/>.</summary>
public static class StartingContextCodes
{
    /// <summary>The starting input context is not an input mapping context.</summary>
    public const string NotAContext = DiagnosticCodes.InputPrefix + "StartingContext.NotAContext";

    /// <summary>The context is the project's, and the input source it is named under does not declare it.</summary>
    public const string Undeclared = DiagnosticCodes.InputPrefix + "StartingContext.Undeclared";
}
