// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

[assembly:
    SuppressMessage(
        "Style",
        "IDE0130:Namespace does not match folder structure",
        Justification = "We want all controls to be under the namespace DroidNet.Controls",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Controls")]
[assembly:
    SuppressMessage(
        "Naming",
        "CA1707:Identifiers should not contain underscores",
        Justification = "Underscores are useful for event handler naming")]
