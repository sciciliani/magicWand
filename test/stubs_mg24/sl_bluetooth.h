// Stand-in for the native Silicon Labs Bluetooth API (sl_bt_api.h), only the
// parts comm.cpp uses — ONLY for the host compile check (make check-fw).
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t sl_status_t;
#define SL_STATUS_OK ((sl_status_t)0x0000)
#define SL_STATUS_NO_MORE_RESOURCE ((sl_status_t)0x001A)
#define SL_BT_INVALID_CONNECTION_HANDLE ((uint8_t)0xFF)
typedef struct { uint8_t len; uint8_t data[]; } uint8array;
typedef struct { uint8_t addr[6]; } bd_addr;
typedef struct { uint8_t data[16]; } uuid_128;
typedef struct { uint8_t data[2]; } sl_bt_uuid_16_t;

#define SL_BT_GATTDB_ADVERTISED_SERVICE 0x1
#define SL_BT_GATTDB_CHARACTERISTIC_READ 0x2
#define SL_BT_GATTDB_CHARACTERISTIC_WRITE_NO_RESPONSE 0x4
#define SL_BT_GATTDB_CHARACTERISTIC_WRITE 0x8
#define SL_BT_GATTDB_CHARACTERISTIC_NOTIFY 0x10
enum { sl_bt_gattdb_primary_service = 0x0 };
enum { sl_bt_gattdb_fixed_length_value = 0x1, sl_bt_gattdb_variable_length_value = 0x2 };
enum { sl_bt_gatt_server_client_config = 0x1 };
enum { sl_bt_gatt_server_notification = 0x1 };
enum { sl_bt_advertiser_general_discoverable = 0x2 };
enum { sl_bt_advertiser_connectable_scannable = 0x2 };

#define SL_BT_MSG_ID(HDR) ((HDR) & 0xffff00f8u)
#define sl_bt_evt_system_boot_id 0x000100a0
#define sl_bt_evt_connection_opened_id 0x000600a0
#define sl_bt_evt_connection_closed_id 0x010600a0
#define sl_bt_evt_gatt_mtu_exchanged_id 0x000900a0
#define sl_bt_evt_gatt_server_attribute_value_id 0x000a00a0
#define sl_bt_evt_gatt_server_characteristic_status_id 0x030a00a0

typedef struct { bd_addr address; uint8_t address_type, role, connection, bonding, advertiser; uint16_t sync; } sl_bt_evt_connection_opened_t;
typedef struct { uint16_t reason; uint8_t connection; } sl_bt_evt_connection_closed_t;
typedef struct { uint8_t connection; uint16_t mtu; } sl_bt_evt_gatt_mtu_exchanged_t;
typedef struct { uint8_t connection; uint16_t characteristic; uint8_t status_flags; uint16_t client_config_flags, client_config; } sl_bt_evt_gatt_server_characteristic_status_t;
typedef struct { uint8_t connection; uint16_t attribute; uint8_t att_opcode; uint16_t offset; uint8array value; } sl_bt_evt_gatt_server_attribute_value_t;
typedef struct {
  uint32_t header;
  union {
    sl_bt_evt_connection_opened_t evt_connection_opened;
    sl_bt_evt_connection_closed_t evt_connection_closed;
    sl_bt_evt_gatt_mtu_exchanged_t evt_gatt_mtu_exchanged;
    sl_bt_evt_gatt_server_characteristic_status_t evt_gatt_server_characteristic_status;
    sl_bt_evt_gatt_server_attribute_value_t evt_gatt_server_attribute_value;
  } data;
} sl_bt_msg_t;

sl_status_t sl_bt_gattdb_new_session(uint16_t* session);
sl_status_t sl_bt_gattdb_add_service(uint16_t session, uint8_t type, uint8_t property, size_t uuid_len, const uint8_t* uuid, uint16_t* service);
sl_status_t sl_bt_gattdb_add_uuid16_characteristic(uint16_t session, uint16_t service, uint16_t property, uint16_t security, uint8_t flag, sl_bt_uuid_16_t uuid, uint8_t value_type, uint16_t maxlen, size_t value_len, const uint8_t* value, uint16_t* characteristic);
sl_status_t sl_bt_gattdb_add_uuid128_characteristic(uint16_t session, uint16_t service, uint16_t property, uint16_t security, uint8_t flag, uuid_128 uuid, uint8_t value_type, uint16_t maxlen, size_t value_len, const uint8_t* value, uint16_t* characteristic);
sl_status_t sl_bt_gattdb_start_service(uint16_t session, uint16_t service);
sl_status_t sl_bt_gattdb_commit(uint16_t session);
sl_status_t sl_bt_gattdb_abort(uint16_t session);
sl_status_t sl_bt_gatt_server_set_max_mtu(uint16_t max_mtu, uint16_t* max_mtu_out);
sl_status_t sl_bt_gatt_server_send_notification(uint8_t connection, uint16_t characteristic, size_t value_len, const uint8_t* value);
sl_status_t sl_bt_advertiser_create_set(uint8_t* handle);
sl_status_t sl_bt_advertiser_set_timing(uint8_t advertising_set, uint32_t interval_min, uint32_t interval_max, uint16_t duration, uint8_t maxevents);
sl_status_t sl_bt_advertiser_stop(uint8_t advertising_set);
sl_status_t sl_bt_legacy_advertiser_generate_data(uint8_t advertising_set, uint8_t discover);
sl_status_t sl_bt_legacy_advertiser_start(uint8_t advertising_set, uint8_t connect);
sl_status_t sl_bt_connection_close(uint8_t connection);
void sl_bt_on_event(sl_bt_msg_t* evt);
