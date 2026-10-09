#pragma once
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
inline void phoneBleMemory(const char* stage) {
  esp_rom_printf("[RoboDeskBLE] stage=%s heap=%u largest=%u minimum=%u\n", stage,
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
}

// Keep the transport/security policy independent of the source-built host API.
#if defined(ROBODESK_NIMBLE_EXTERNAL)
#include <NimBLEDevice.h>
#include <nimble/nimble/host/include/host/ble_hs.h>
// Disable upstream's name aliases before declaring our policy adapter.
#undef BLEDevice
#undef BLEServer
#undef BLEService
#undef BLECharacteristic
#undef BLEAdvertising
#undef BLEAdvertisementData
#undef BLEServerCallbacks
#undef BLECharacteristicCallbacks
using BLEServer = NimBLEServer;
using BLEService = NimBLEService;
using BLECharacteristic = NimBLECharacteristic;
using BLEAdvertising = NimBLEAdvertising;
using BLEAdvertisementData = NimBLEAdvertisementData;
struct BLESecurityCallbacks {
  virtual ~BLESecurityCallbacks() = default;
  virtual void onPassKeyNotify(uint32_t) {}
  virtual void onAuthenticationComplete(ble_gap_conn_desc*) {}
};
namespace PhoneBleBackend {
inline BLESecurityCallbacks* security = nullptr;
inline uint32_t passkey = 0;
inline ble_gap_conn_desc descriptor(const NimBLEConnInfo& info) {
  ble_gap_conn_desc d{};
  d.conn_handle = info.getConnHandle();
  d.peer_id_addr = *info.getIdAddress().getBase();
  d.peer_ota_addr = *info.getAddress().getBase();
  d.sec_state.encrypted = info.isEncrypted();
  d.sec_state.authenticated = info.isAuthenticated();
  d.sec_state.bonded = info.isBonded();
  d.sec_state.key_size = info.getSecKeySize();
  return d;
}
}
class BLEServerCallbacks : public NimBLEServerCallbacks {
 public:
  virtual void onConnect(BLEServer*, ble_gap_conn_desc*) {}
  virtual void onDisconnect(BLEServer*, ble_gap_conn_desc*) {}
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) final {
    auto d = PhoneBleBackend::descriptor(info); onConnect(s, &d);
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int) final {
    auto d = PhoneBleBackend::descriptor(info); onDisconnect(s, &d);
  }
  uint32_t onPassKeyDisplay() final {
    // Fresh code per pairing attempt so a failed attempt reveals nothing about the next one.
    uint32_t raw = 0;
    do { esp_fill_random(&raw, sizeof(raw)); } while (raw >= 4294000000u);
    uint32_t code = raw % 1000000u;
    if (!code) code = 1;
    PhoneBleBackend::passkey = code;
    if (PhoneBleBackend::security) PhoneBleBackend::security->onPassKeyNotify(code);
    return code;
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) final {
    auto d = PhoneBleBackend::descriptor(info);
    if (PhoneBleBackend::security) PhoneBleBackend::security->onAuthenticationComplete(&d);
  }
};
class BLECharacteristicCallbacks : public NimBLECharacteristicCallbacks {
 public:
  virtual void onRead(BLECharacteristic*, ble_gap_conn_desc*) {}
  virtual void onWrite(BLECharacteristic*, ble_gap_conn_desc*) {}
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& info) final {
    auto d = PhoneBleBackend::descriptor(info); onRead(c, &d);
  }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) final {
    auto d = PhoneBleBackend::descriptor(info); onWrite(c, &d);
  }
};
struct BLEDevice {
  static bool init(const char* name) { return NimBLEDevice::init(name); }
  static bool setMTU(uint16_t mtu) { return NimBLEDevice::setMTU(mtu); }
  static void setSecurityCallbacks(BLESecurityCallbacks* cb) { PhoneBleBackend::security = cb; }
  static BLEServer* createServer() { return NimBLEDevice::createServer(); }
  static BLEAdvertising* getAdvertising() { return NimBLEDevice::getAdvertising(); }
};
struct BLESecurity {
  static void setAuthenticationMode(bool bond, bool mitm, bool sc) { NimBLEDevice::setSecurityAuth(bond, mitm, sc); }
  static void setCapability(uint8_t cap) { NimBLEDevice::setSecurityIOCap(cap); }
  static void setPassKey(bool, uint32_t code) {
    PhoneBleBackend::passkey = code;
    // 2.5.1 invokes onPassKeyDisplay only for this sentinel. The callback
    // supplies our random code and forwards it to the existing OLED handshake.
    NimBLEDevice::setSecurityPasskey(123456);
  }
  static bool startSecurity(uint16_t connection) { return NimBLEDevice::startSecurity(connection); }
};
namespace PhoneBleProperties {
inline constexpr auto READ = NIMBLE_PROPERTY::READ;
inline constexpr auto READ_ENC = NIMBLE_PROPERTY::READ_ENC;
inline constexpr auto WRITE = NIMBLE_PROPERTY::WRITE;
inline constexpr auto WRITE_NR = NIMBLE_PROPERTY::WRITE_NR;
inline constexpr auto WRITE_ENC = NIMBLE_PROPERTY::WRITE_ENC;
inline constexpr auto NOTIFY = NIMBLE_PROPERTY::NOTIFY;
}
// Encryption is enforced by READ_ENC/WRITE_ENC creation flags; the policy
// callbacks additionally require MITM, a 16-byte key and the trusted peer.
inline void phoneBleReadEncrypted(BLECharacteristic*) {}
inline void phoneBleWriteEncrypted(BLECharacteristic*) {}
inline void phoneBleCccd(BLECharacteristic*) {} // NimBLE creates CCCDs automatically.
inline bool phoneBleNotify(BLECharacteristic* c, uint16_t connection) { return c->notify(connection); }
inline void phoneBleScanData(BLEAdvertisementData& data, const uint8_t* bytes, size_t size) { data.addData(bytes, size); }
inline void phoneBleServerCallbacks(BLEServer* server, BLEServerCallbacks* cb) { server->setCallbacks(cb, false); }
#else
#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
namespace PhoneBleProperties {
inline constexpr auto READ = BLECharacteristic::PROPERTY_READ;
inline constexpr auto READ_ENC = BLECharacteristic::PROPERTY_READ_ENC;
inline constexpr auto WRITE = BLECharacteristic::PROPERTY_WRITE;
inline constexpr auto WRITE_NR = BLECharacteristic::PROPERTY_WRITE_NR;
inline constexpr auto WRITE_ENC = BLECharacteristic::PROPERTY_WRITE_ENC;
inline constexpr auto NOTIFY = BLECharacteristic::PROPERTY_NOTIFY;
}
inline void phoneBleReadEncrypted(BLECharacteristic* c) { c->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED); }
inline void phoneBleWriteEncrypted(BLECharacteristic* c) { c->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED); }
inline void phoneBleCccd(BLECharacteristic* c) { c->addDescriptor(new BLE2902()); }
inline bool phoneBleNotify(BLECharacteristic* c, uint16_t) { c->notify(true); return true; }
inline void phoneBleScanData(BLEAdvertisementData& data, const uint8_t* bytes, size_t size) { data.addData(String(reinterpret_cast<const char*>(bytes), size)); }
inline void phoneBleServerCallbacks(BLEServer* server, BLEServerCallbacks* cb) { server->setCallbacks(cb); }
#endif
