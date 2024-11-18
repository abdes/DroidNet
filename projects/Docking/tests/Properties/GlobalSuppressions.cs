// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

[assembly:
    SuppressMessage(
        "Naming",
        "CA1707:Identifiers should not contain underscores",
        Justification = "Test method names are more readable with underscores",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Docking.Tests")]

[assembly:
    SuppressMessage(
        "StyleCop.CSharp.DocumentationRules",
        "SA1600:Elements should be documented",
        Justification = "Test cases do not require XMLDoc comments",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Docking.Tests")]

[assembly:
    SuppressMessage(
        "StyleCop.CSharp.DocumentationRules",
        "SA1601:Partial elements should be documented",
        Justification = "Test cases do not require XMLDoc comments",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Docking.Tests")]
