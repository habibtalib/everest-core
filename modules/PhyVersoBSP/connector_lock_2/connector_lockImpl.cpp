// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "connector_lockImpl.hpp"

namespace module {
namespace connector_lock_2 {

void connector_lockImpl::init() {
}

void connector_lockImpl::ready() {
}

void connector_lockImpl::handle_lock() {
    mod->serial.lock(2, true);
}

void connector_lockImpl::handle_unlock() {
    mod->serial.lock(2, false);
}

} // namespace connector_lock_2
} // namespace module
