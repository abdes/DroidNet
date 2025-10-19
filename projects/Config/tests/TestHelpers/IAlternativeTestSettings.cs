// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Interface for alternative test settings for multi-service testing.
/// </summary>
public interface IAlternativeTestSettings : INotifyPropertyChanged
{
    string Theme { get; set; }

    [Range(8, 72)]
    int FontSize { get; set; }

    bool AutoSave { get; set; }
}
