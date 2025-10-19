// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Interface for test settings with common properties.
/// </summary>
public interface ITestSettings : INotifyPropertyChanged
{
    [Required]
    [StringLength(100, MinimumLength = 1)]
    string Name { get; set; }

    [Range(0, 1000)]
    int Value { get; set; }

    bool IsEnabled { get; set; }

    string? Description { get; set; }
}
