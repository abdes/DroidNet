// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Represents the configuration settings for the PathFinder application.
/// </summary>
/// <param name="Mode">The mode in which the application is running.</param>
/// <param name="CompanyName">The name of the company using the application.</param>
/// <param name="ApplicationName">The name of the application.</param>
public readonly record struct PathFinderConfig(string Mode, string CompanyName, string ApplicationName);
