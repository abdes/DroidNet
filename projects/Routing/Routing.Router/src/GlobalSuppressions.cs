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
        Target = "~N:DroidNet.Routing")]

[assembly:
    SuppressMessage(
        "Design",
        "CA1055:URI parameters should not be strings",
        Justification = "The router urls are not like other urls",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing")]

[assembly:
    SuppressMessage(
        "Design",
        "CA1054:URI-like parameters should not be strings",
        Justification = "The router urls are not like other urls",
        Scope = "namespaceanddescendants",
        Target = "~N:DroidNet.Routing")]
