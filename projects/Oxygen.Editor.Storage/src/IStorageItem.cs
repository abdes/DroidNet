// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

public interface IStorageItem
{
    string Name { get; }

    string Location { get; }

    DateTime LastModifiedTime { get; }
}
