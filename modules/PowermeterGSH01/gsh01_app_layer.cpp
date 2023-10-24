// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest

#include "gsh01_app_layer.hpp"

#include <cstring>
#include <everest/logging.hpp>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

#include <sys/select.h>
#include <sys/time.h>

#include <fmt/core.h>


namespace gsh01_app_layer {

uint32_t timepoint_to_uint32(date::utc_clock::time_point timepoint) {
    return (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(timepoint.time_since_epoch()).count();
}

void insert_u16_as_u8s(std::vector<uint8_t>& vec, uint16_t u16) {
    uint8_t upper = uint8_t((u16 >> 8) & 0x00FF);
    uint8_t lower = uint8_t(u16 & 0x00FF);
    vec.push_back(lower);
    vec.push_back(upper);
}

void insert_u32_as_u8s(std::vector<uint8_t>& vec, uint32_t u32) {
    vec.push_back(uint8_t( u32        & 0x000000FF));
    vec.push_back(uint8_t((u32 >> 8)  & 0x000000FF));
    vec.push_back(uint8_t((u32 >> 16) & 0x000000FF));
    vec.push_back(uint8_t((u32 >> 24) & 0x000000FF));
}

std::vector<uint8_t> Gsh01AppLayer::create_command(gsh01_app_layer::Command cmd) {
    std::vector<uint8_t> command_data{};

    insert_u16_as_u8s(command_data, (uint16_t)cmd.type);
    insert_u16_as_u8s(command_data, (uint16_t)cmd.length);
    command_data.push_back((uint8_t)gsh01_app_layer::CommandStatus::OK);
    
    for (uint16_t i = 0; i < cmd.data.size(); i++) {
        command_data.push_back(cmd.data[i]);
    }
    return std::move(command_data);
}

std::vector<uint8_t> Gsh01AppLayer::create_simple_command(gsh01_app_layer::CommandType cmd_type) {
    gsh01_app_layer::Command cmd{};

    cmd.type = cmd_type;
    cmd.length = 0x0005;
    cmd.status = gsh01_app_layer::CommandStatus::OK;

    return std::move(create_command(cmd));
}

int8_t Gsh01AppLayer::get_utc_offset_in_quarter_hours(const std::chrono::time_point<std::chrono::system_clock>& timepoint_system_clock) {
    std::stringstream offset;
    int8_t offset_quarterhours = 0;

    auto tm = std::chrono::system_clock::to_time_t(timepoint_system_clock);
    offset << std::put_time(std::localtime(&tm), "%z");
    int offset_int = std::stoi(offset.str());

    int offset_h = offset_int / 100;
    int offset_remaining = offset_int % 100;  // in case of timezones that are not full-hour offsets of UTC
    if (offset_remaining != 0) {
        int8_t offset_remaining_extra_hour = offset_remaining / 60;
        if (offset_remaining_extra_hour != 0) {
            offset_quarterhours += offset_remaining_extra_hour * 4;  // can be positive or negative
            offset_remaining -= offset_remaining_extra_hour * 4;
        }
        int8_t offset_remaining_quarterhours = offset_remaining / 15;
        offset_quarterhours += offset_remaining_quarterhours;
    }
    offset_quarterhours += offset_h * 4;
    
    return offset_quarterhours;
}

void Gsh01AppLayer::create_command_start_transaction(gsh01_app_layer::UserIdStatus user_id_status,
                                                   gsh01_app_layer::UserIdType user_id_type,
                                                   std::string user_id_data,
                                                   int8_t gmt_offset_quarter_hours,
                                                   std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::START_TRANSACTION;
    cmd.length = 0x0034;
    cmd.status = gsh01_app_layer::CommandStatus::OK;

    insert_u32_as_u8s(cmd.data, (timepoint_to_uint32(date::utc_clock::now())));
    cmd.data.push_back(static_cast<uint8_t>(gmt_offset_quarter_hours)); // GMT offset in quarters of an hour, e.g. 0x08 = +2h
    cmd.data.push_back((uint8_t)user_id_status);
    cmd.data.push_back((uint8_t)user_id_type);

    uint8_t byte_count = 0;
    for (uint8_t databyte : user_id_data) {  // push up to 40 characters of user id name into command
        cmd.data.push_back(databyte);
        byte_count++;
        if (byte_count >= 40) break;
    }
    while(byte_count < 40){                 // fill remaining user id name characters with zeros
        cmd.data.push_back(0x00);
        byte_count++;
    }

    command_data = std::move(create_command(cmd));
}

void Gsh01AppLayer::create_command_stop_transaction(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::STOP_TRANSACTION));
}

void Gsh01AppLayer::create_command_get_time(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::TIME));
}

void Gsh01AppLayer::create_command_set_time(date::utc_clock::time_point timepoint,
                                          int8_t gmt_offset_quarters_of_an_hour,
                                          std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::TIME;
    cmd.length = 0x000A;
    cmd.status = gsh01_app_layer::CommandStatus::OK;
    
    insert_u32_as_u8s(cmd.data, timepoint_to_uint32(timepoint));
    cmd.data.push_back(gmt_offset_quarters_of_an_hour);

    command_data = std::move(create_command(cmd));
}
void Gsh01AppLayer::create_command_get_bus_address(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::METER_BUS_ADDR));
}

void Gsh01AppLayer::create_command_set_bus_address(uint8_t bus_address,
                                                   std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::METER_BUS_ADDR;
    cmd.length = 0x0006;
    cmd.status = gsh01_app_layer::CommandStatus::OK;
    
    cmd.data.push_back(bus_address);

    command_data = std::move(create_command(cmd));
}

void Gsh01AppLayer::create_command_get_voltage(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_VOLTAGE_L1));
}

void Gsh01AppLayer::create_command_get_current(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_CURRENT_L1));
}

void Gsh01AppLayer::create_command_get_import_power(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_IMPORT_DEV_POWER));
}

void Gsh01AppLayer::create_command_get_total_dev_import_energy(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_TOTAL_IMPORT_DEV_ENERGY));
}

void Gsh01AppLayer::create_command_get_total_power(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_TOTAL_DEV_POWER));
}

void Gsh01AppLayer::create_command_get_total_start_import_energy(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_TOTAL_START_IMPORT_DEV_ENERGY));
}

void Gsh01AppLayer::create_command_get_total_stop_import_energy(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_TOTAL_STOP_IMPORT_DEV_ENERGY));
}

void Gsh01AppLayer::create_command_get_total_transaction_duration(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_TRANSACT_TOTAL_DURATION));
}

void Gsh01AppLayer::create_command_get_pubkey_str16(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_PUBKEY_STR16));
}

void Gsh01AppLayer::create_command_get_pubkey_asn1(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_PUBKEY_ASN1));
}

void Gsh01AppLayer::create_command_get_ocmf_stats(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::OCMF_STATS));
}

/* OCMF ID: 1..235000 
    OCMF data from specified transaction will be at minimum import energy of transaction
*/
void Gsh01AppLayer::create_command_get_transaction_ocmf(uint32_t ocmf_id,
                                                      std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::GET_OCMF;
    cmd.length = 0x0009;
    cmd.status = gsh01_app_layer::CommandStatus::OK;
    
    insert_u32_as_u8s(cmd.data, ocmf_id);

    command_data = std::move(create_command(cmd));
}

void Gsh01AppLayer::create_command_get_last_transaction_ocmf(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_LAST_OCMF));
}

void Gsh01AppLayer::create_command_get_charge_point_id(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::CHARGE_POINT_ID));
}

/* only works in "assembly mode" */
void Gsh01AppLayer::create_command_set_charge_point_id(gsh01_app_layer::UserIdType id_type,
                                                     std::string id_data,
                                                     std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::CHARGE_POINT_ID;
    cmd.length = 0x0013;
    cmd.status = gsh01_app_layer::CommandStatus::OK;

    cmd.data.push_back((uint8_t)id_type);
    
    uint8_t byte_count = 0;
    for (uint8_t databyte : id_data) {      // push up to 13 characters of id data into command
        cmd.data.push_back(databyte);
        byte_count++;
        if (byte_count >= 13) break;
    }
    while(byte_count < 13){                 // fill remaining user id name characters with zeros
        cmd.data.push_back(0x00);
        byte_count++;
    }

    command_data = std::move(create_command(cmd));
}

void Gsh01AppLayer::create_command_get_log_stats(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_LOG_STATS));
}

/* log entry ids: 1..2500 */
void Gsh01AppLayer::create_command_get_log_entry(uint32_t log_entry_id,
                                               std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::GET_LOG_ENTRY;
    cmd.length = 0x0009;
    cmd.status = gsh01_app_layer::CommandStatus::OK;
    
    insert_u32_as_u8s(cmd.data, log_entry_id);

    command_data = std::move(create_command(cmd));
}

void Gsh01AppLayer::create_command_get_last_log_entry(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::GET_LAST_LOG_ENTRY));
}

/* log entry ids: 1..2500 
   thus: if 20 log entries and log_entry_id == 2, then log entry 18 will be returned 
*/
void Gsh01AppLayer::create_command_get_log_entry_reverse(uint32_t log_entry_id,
                                                       std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::GET_LOG_ENTRY_REVERSE;
    cmd.length = 0x0009;
    cmd.status = gsh01_app_layer::CommandStatus::OK;
    
    insert_u32_as_u8s(cmd.data, log_entry_id);

    command_data = std::move(create_command(cmd));
}

void Gsh01AppLayer::create_command_get_application_board_mode(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_MODE_SET));
}

void Gsh01AppLayer::create_command_set_application_board_mode(gsh01_app_layer::ApplicationBoardMode mode,
                                                            std::vector<uint8_t>& command_data) {
    gsh01_app_layer::Command cmd{};

    cmd.type = gsh01_app_layer::CommandType::AB_MODE_SET;
    cmd.length = 0x0006;
    cmd.status = gsh01_app_layer::CommandStatus::OK;
    
    cmd.data.push_back((uint8_t)mode);

    command_data = std::move(create_command(cmd));
}

// diagnostics

void Gsh01AppLayer::create_command_get_hardware_version(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_HW_VERSION));
}

void Gsh01AppLayer::create_command_get_application_board_server_id(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_SERVER_ID));
}

void Gsh01AppLayer::create_command_get_application_board_serial_number(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_SERIAL_NR));
}

void Gsh01AppLayer::create_command_get_application_board_software_version(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_SW_VERSION));
}

void Gsh01AppLayer::create_command_get_application_board_fw_checksum(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_FW_CHECKSUM));
}

void Gsh01AppLayer::create_command_get_application_board_fw_hash(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_FW_HASH));
}

void Gsh01AppLayer::create_command_get_application_board_status(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_STATUS));
}

void Gsh01AppLayer::create_command_get_metering_board_software_version(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::MB_SW_VERSION));
}

void Gsh01AppLayer::create_command_get_metering_board_fw_checksum(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::MB_FW_CHECKSUM));
}

void Gsh01AppLayer::create_command_get_bootloader_version(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::BOOTL_VERSION));
}

/* doubles as OCMF "meter model name" */
void Gsh01AppLayer::create_command_get_device_type(std::vector<uint8_t>& command_data) {
    command_data = std::move(create_simple_command(gsh01_app_layer::CommandType::AB_DEVICE_TYPE));
}

} // namespace gsh01_app_layer