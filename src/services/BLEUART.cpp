#include "services/BLEUART.h"
#include "infra/Logger.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEUART *BLEUART::s_inst = nullptr;

static BLECharacteristic *g_tx = nullptr;

class BleSrvCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *s) override {
        if (BLEUART::instance()) BLEUART::instance()->onConnected(true);
    }
    void onDisconnect(BLEServer *s) override {
        if (BLEUART::instance()) BLEUART::instance()->onConnected(false);
        BLEDevice::startAdvertising();
    }
};

class BleRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!BLEUART::instance()) return;
        String val = c->getValue();
        BLEUART::instance()->onRxData((const uint8_t *)val.c_str(), val.length());
    }
};

static BleSrvCallbacks s_srvCb;
static BleRxCallbacks s_rxCb;

void BLEUART::begin(const char *name) {
    s_inst = this;
    m_out.setHook(&BLEUART::sendHook);

    /* Single-producer/single-consumer ring for bytes from the BLE host task
     * to the CLI task. Replaces the previous unsynchronised std::deque. */
    if (!m_fifo) m_fifo = xStreamBufferCreate(512, 1);

    BLEDevice::init(name);
    esp_err_t mtuErr = BLEDevice::setMTU(517);
    if (mtuErr != ESP_OK) LOG_WARN("BLE MTU set failed: %d", (int)mtuErr);
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(&s_srvCb);

    BLEService *service = server->createService(BLE_SERVICE_UUID);
    BLECharacteristic *rx = service->createCharacteristic(
        BLE_CHAR_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(&s_rxCb);
    g_tx = service->createCharacteristic(BLE_CHAR_TX, BLECharacteristic::PROPERTY_NOTIFY);
    service->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();

    LOG_INFO("BLE UART advertising as %s", name);
}

size_t BLEUART::available() {
    return m_fifo ? xStreamBufferBytesAvailable(m_fifo) : 0;
}

int BLEUART::read() {
    uint8_t b = 0;
    if (m_fifo && xStreamBufferReceive(m_fifo, &b, 1, 0) == 1) return b;
    return -1;
}

void BLEUART::onRxData(const uint8_t *data, size_t len) {
    if (m_fifo && len > 0)
        xStreamBufferSend(m_fifo, data, len, 0);   /* drops excess if full */
}

void BLEUART::onConnected(bool c) { m_conn = c; }

void BLEUART::sendHook(const char *s) {
    if (!BLEUART::instance()) return;
    BLEUART *me = BLEUART::instance();
    if (!me->m_conn || !g_tx || !s) return;

    size_t len = strlen(s);
    if (len == 0) return;

    /* ATT notification payload is MTU - 3. Before negotiation use 20 bytes. */
    uint16_t mtu = BLEDevice::getMTU();
    size_t chunk = (mtu > 23) ? (size_t)(mtu - 3) : 20;
    if (chunk > 512) chunk = 512;
    if (chunk == 0) chunk = 20;

    for (size_t off = 0; off < len; off += chunk) {
        size_t n = len - off;
        if (n > chunk) n = chunk;
        g_tx->setValue((uint8_t *)(s + off), n);
        g_tx->notify();
        if (off + n < len) delay(5);   /* give the BLE stack TX buffers time */
    }
}


