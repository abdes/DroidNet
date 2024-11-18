// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

[assembly:
    SuppressMessage(
        "Design",
        "CA1056:URI-like properties should not be strings",
        Justification = "The router urls are not like other urls",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]

[assembly:
    SuppressMessage(
        "Design",
        "CA1055:URI parameters should not be strings",
        Justification = "The router urls are not like other urls",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]

[assembly:
    SuppressMessage(
        "Design",
        "CA1054:URI-like parameters should not be strings",
        Justification = "The router urls are not like other urls",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]

[assembly:
    SuppressMessage(
        "Naming",
        "CA1707:Identifiers should not contain underscores",
        Justification = "Test method names are more readable with underscores",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]

[assembly:
    SuppressMessage(
        "StyleCop.CSharp.DocumentationRules",
        "SA1600:Elements should be documented",
        Justification = "Test cases do not require XMLDoc comments",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]

[assembly:
    SuppressMessage(
        "StyleCop.CSharp.DocumentationRules",
        "SA1601:Partial elements should be documented",
        Justification = "Test cases do not require XMLDoc comments",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]

[assembly:
    SuppressMessage(
        "Maintainability",
        "CA1515:Consider making public types internal",
        Justification = "test classes need to be public",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing.Tests")]
