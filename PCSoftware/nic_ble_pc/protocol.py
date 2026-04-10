# GATT UUIDs (16-bit vendor IDs in BLE base UUID)
# See doc/ble_protocol.md

SERVICE_UUID = "0000ff50-0000-1000-8000-00805f9b34fb"
CHAR_RX_UUID = "0000ff51-0000-1000-8000-00805f9b34fb"  # write JSON (phone -> device)
CHAR_TX_UUID = "0000ff52-0000-1000-8000-00805f9b34fb"  # notify (device -> phone)

DEVICE_NAME_HINTS = ("4G_NIC_CFG", "4G_NIC")
