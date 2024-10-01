// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

[assembly:
    SuppressMessage(
        "Style",
        "IDE0130:Namespace does not match folder structure",
        Justification = "all controls are under the namespace DroidNet.Controls",
        Scope = "namespace",
        Target = "~N:DroidNet.Controls.Tests")]

[assembly:
    SuppressMessage(
        "Naming",
        "CA1707:Identifiers should not contain underscores",
        Justification = "Test method names are more readable with underscores",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Controls.Tests")]
