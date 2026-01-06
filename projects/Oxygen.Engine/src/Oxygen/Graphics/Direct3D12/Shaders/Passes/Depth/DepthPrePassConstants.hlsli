//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// Pass-level payload for the depth pre-pass.
// Accessed via RootConstants.g_PassConstantsIndex as a bindless CBV.
struct DepthPrePassConstants
{
    float alpha_cutoff_default;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};
