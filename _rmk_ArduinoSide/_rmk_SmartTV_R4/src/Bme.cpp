#include "Bme.h"
#include "Globals.h"
#include "AppLink.h"
#include "Debug.h"
#include <math.h>

namespace BME {

/** Starts I2C, probes the sensor at 0x76/0x77, applies its settings and
    performs an initial read, then registers Check() as a repeating task
    every CheckTIME seconds. Call once in setup(). Falls back to the
    library's default begin() (and a full I2C address scan for the debug
    log) if neither address ACKs. */
void Setup() {
    Wire.begin();
    uint8_t detectedAddr = 0;
    for (uint8_t a = 0x76; a <= 0x77; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            detectedAddr = a;
            break;
        }
    }

    if (detectedAddr) {
        BME280I2C::Settings s(
            BME280::OSR_X16, BME280::OSR_X1, BME280::OSR_X1,
            BME280::Mode_Normal, BME280::StandbyTime_500us,
            BME280::Filter_16, BME280::SpiEnable_False,
            (detectedAddr == 0x76) ? BME280I2C::I2CAddr_0x76 : BME280I2C::I2CAddr_0x77
        );

        bme280sensor.setSettings(s);
        if (bme280sensor.begin()) {
            float temp = 0.0f, hum = 0.0f, pres = 0.0f;
            BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
            BME280::PresUnit presUnit(BME280::PresUnit_Pa);
            bme280sensor.read(pres, temp, hum, tempUnit, presUnit);

            BME::State.Temperature = temp;
            BME::State.Humidity = hum;

            APP::termMsgLog(APP_LOG_INF, APP_SRC_BME, "BME", "Setup", "sensor found on I2C address [0x%02X] temperature [%.1f C] humidity [%.1f %%]", detectedAddr, temp, hum);
        } else {
            APP::termMsgLog(APP_LOG_ERR, APP_SRC_BME, "BME", "Setup", "ACKed at I2C 0x%02X but library init() failed", detectedAddr);
        }
    } else {
        if (bme280sensor.begin()) {
            float temp = 0.0f, hum = 0.0f, pres = 0.0f;
            BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
            BME280::PresUnit presUnit(BME280::PresUnit_Pa);
            bme280sensor.read(pres, temp, hum, tempUnit, presUnit);

            BME::State.Temperature = temp;
            BME::State.Humidity = hum;

            APP::termMsgLog(APP_LOG_INF, APP_SRC_BME, "BME", "Setup", "sensor found (library default) temperature [%.1f C] humidity [%.1f %%]", temp, hum);
        } else {
            APP::termMsgLog(APP_LOG_ERR, APP_SRC_BME, "BME", "Setup", "sensor NOT found on I2C (no ACK on 0x76/0x77)");
        }
    }

    SCHED::AddTask("BME280_Setup", "Check", Check, SCHED::Unit::S, CheckTIME, 1, true);
}

/** Periodic sensor read task - updates BME::State.Temperature/Humidity and
    pushes to the app if either value changed since the last read. Called
    automatically by the scheduler every CheckTIME seconds. */
void Check(SCHED::TaskId taskId) {
    (void)taskId;
    float temp(NAN), hum(NAN), pres(NAN);
    BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
    BME280::PresUnit presUnit(BME280::PresUnit_Pa);
    bme280sensor.read(pres, temp, hum, tempUnit, presUnit);

    bool n = false;
    if (BME::State.Temperature != temp) {
        BME::State.Temperature = temp;
        n = true;
    }
    if (BME::State.Humidity != hum) {
        BME::State.Humidity = hum;
        n = true;
    }

    if (n) {
        APP::updStatus("NET::Check");
    }
}

} // namespace BME
