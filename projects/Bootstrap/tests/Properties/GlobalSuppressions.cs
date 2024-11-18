// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

// @formatter:off
// ReSharper disable StringLiteralTypo
[assembly:
    SuppressMessage(
        "Security",
        "CA5392:Use DefaultDllImportSearchPaths attribute for P/Invokes",
        Justification = "external code, not under our control",
        Scope = "member",
        Target = "~M:Microsoft.Windows.Foundation.UndockedRegFreeWinRTCS.NativeMethods.WindowsAppRuntime_EnsureIsLoaded~System.Int32")]

// ReSharper disable StringLiteralTypo
// @formatter:on
[assembly:
    SuppressMessage(
        "Naming",
        "CA1707:Identifiers should not contain underscores",
        Justification = "Test method names are more readable with underscores",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Bootstrap.Tests")]

[assembly:
    SuppressMessage(
        "StyleCop.CSharp.DocumentationRules",
        "SA1600:Elements should be documented",
        Justification = "Test cases do not require XMLDoc comments",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Bootstrap.Tests")]

[assembly:
    SuppressMessage(
        "StyleCop.CSharp.DocumentationRules",
        "SA1601:Partial elements should be documented",
        Justification = "Test cases do not require XMLDoc comments",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Bootstrap.Tests")]

[assembly:
    SuppressMessage(
        "Maintainability",
        "CA1515:Consider making public types internal",
        Justification = "test classes need to be public",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Bootstrap.Tests")]
