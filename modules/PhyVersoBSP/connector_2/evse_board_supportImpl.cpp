// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "evse_board_supportImpl.hpp"

namespace module {
namespace connector_2 {

void evse_board_supportImpl::init() {
    {
        std::scoped_lock lock(caps_mutex);

        caps.min_current_A_import = mod->config.conn2_min_current_A_import;
        caps.max_current_A_import = mod->config.conn2_max_current_A_import;
        caps.min_phase_count_import = mod->config.conn2_min_phase_count_import;
        caps.max_phase_count_import = mod->config.conn2_max_phase_count_import;
        caps.supports_changing_phases_during_charging = false;

        caps.min_current_A_export = mod->config.conn2_min_current_A_export;
        caps.max_current_A_export = mod->config.conn2_max_current_A_export;
        caps.min_phase_count_export = mod->config.conn2_min_phase_count_export;
        caps.max_phase_count_export = mod->config.conn2_max_phase_count_export;
        caps.supports_changing_phases_during_charging = false;

        if (mod->config.conn2_has_socket) {
            caps.connector_type = types::evse_board_support::Connector_type::IEC62196Type2Socket;
        } else {
            caps.connector_type = types::evse_board_support::Connector_type::IEC62196Type2Cable;
        }
    }

    mod->serial.signal_cp_state.connect([this](int connector, CpState s) {
        if (connector == 2 and s not_eq last_cp_state) {
            publish_event(to_bsp_event(s));
            EVLOG_info << "[2] CP State changed: " << to_bsp_event(s);
            last_cp_state = s;
        }
    });
    mod->serial.signal_set_coil_state_response.connect([this](int connector, CoilState s) {
        if (connector == 1) {
            EVLOG_info << "[1] Relais: " << (s.coil_state ? "ON" : "OFF");
            publish_event(to_bsp_event(s));
        }
    });
}

void evse_board_supportImpl::ready() {
}

void evse_board_supportImpl::handle_setup(bool& three_phases, bool& has_ventilation, std::string& country_code) {
    // your code for cmd setup goes here
}

types::evse_board_support::HardwareCapabilities evse_board_supportImpl::handle_get_hw_capabilities() {
    std::scoped_lock lock(caps_mutex);
    return caps;
}

void evse_board_supportImpl::handle_enable(bool& value) {
    if (value) {
        mod->serial.set_pwm(2, 10000);
    } else {
        mod->serial.set_pwm(2, 0);
    }
}

void evse_board_supportImpl::handle_pwm_on(double& value) {
    if (value >= 0 && value <= 100.) {
        mod->serial.set_pwm(2, value * 100);
    }
}

void evse_board_supportImpl::handle_pwm_off() {
    mod->serial.set_pwm(2, 10000);
}

void evse_board_supportImpl::handle_pwm_F() {
    mod->serial.set_pwm(2, 0);
}

void evse_board_supportImpl::handle_allow_power_on(types::evse_board_support::PowerOnOff& value) {
    // ToDo: implement handling of DC coils
    mod->serial.set_coil_state_request(2, CoilType_COIL_AC, value.allow_power_on);
}

void evse_board_supportImpl::handle_ac_switch_three_phases_while_charging(bool& value) {
    // your code for cmd ac_switch_three_phases_while_charging goes here
}

void evse_board_supportImpl::handle_evse_replug(int& value) {
    // your code for cmd evse_replug goes here
}

types::board_support_common::ProximityPilot evse_board_supportImpl::handle_ac_read_pp_ampacity() {
    // IMPLEMENT ME
    return {types::board_support_common::Ampacity::A_32};
}

void evse_board_supportImpl::handle_ac_set_overcurrent_limit_A(double& value) {
    // your code for cmd ac_set_overcurrent_limit_A goes here
}

} // namespace connector_2
} // namespace module
