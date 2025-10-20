// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Interface for alternative test settings for multi-service testing.
/// </summary>
public interface IAlternativeTestSettings : INotifyPropertyChanged
{
    public string Theme { get; set; }

    [Range(8, 72)]
    public int FontSize { get; set; }

    public bool AutoSave { get; set; }
}
