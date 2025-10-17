// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config.Tests;

/// <summary>
///     Interface for test settings.
/// </summary>
public interface ITestSettings
{
    public string FooString { get; set; }

    public int BarNumber { get; set; }
}
