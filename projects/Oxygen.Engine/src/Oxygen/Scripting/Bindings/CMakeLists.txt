# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

set(BINDINGS_CONTRACTS_TARGET "oxygen-scripting-bindings-contracts")
add_library(${BINDINGS_CONTRACTS_TARGET} INTERFACE)
add_library(
  oxygen::scripting-bindings-contracts
  ALIAS ${BINDINGS_CONTRACTS_TARGET}
)
target_sources(
  ${BINDINGS_CONTRACTS_TARGET}
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES "Contracts/IScriptBindingPack.h"
)
target_compile_features(${BINDINGS_CONTRACTS_TARGET} INTERFACE cxx_std_20)

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${BINDINGS_CONTRACTS_TARGET}
)
