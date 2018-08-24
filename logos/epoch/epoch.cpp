//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains definition of the Epoch
///
//===----------------------------------------------------------------------===//
#include <logos/epoch/epoch.hpp>

const size_t Epoch::HASHABLE_BYTES = sizeof(Epoch)
                                            - sizeof(Signature);