// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

/// <summary>Represents a document item, which is always nested within a folder.</summary>
public interface IDocument : INestedItem, IStorageItem;
