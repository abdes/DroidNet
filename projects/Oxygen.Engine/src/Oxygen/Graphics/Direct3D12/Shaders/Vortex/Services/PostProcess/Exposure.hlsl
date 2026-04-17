//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

[numthreads(1, 1, 1)]
void VortexExposureHistogramCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    (void)dispatch_thread_id;
}
