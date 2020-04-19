//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2019-02-28 jp112sdl (Creative Commons)
//- -----------------------------------------------------------------------------------------------------------------------
// #define NDEBUG   // disable all serial debug messages  //necessary to fit 328p!!!
// #define USE_CC1101_ALT_FREQ_86835  //use alternative frequency to compensate not correct working cc1101 modules
#define SENSOR_ONLY

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>
#include <Register.h>

#include <MultiChannelDevice.h>
#include "sensors/sens_bme680.h"
#include "sensors/tmBattery.h"  //SG: added from Tom's UniSensor project
#include "sensors/Sens_SHT31.h"

#define LED_PIN 6
#define CONFIG_BUTTON_PIN 5
#define PEERS_PER_CHANNEL 6
#define SAMPLINGINTERVALL_IN_SECONDS 11  //SG: changed from 240 for testing
#define BATT_SENSOR tmBatteryResDiv<A0, A1, 5700>  //SG: taken from Tom's Unisensor01

//-----------------------------------------------------------------------------------------

//Korrektur von Temperatur und Luftfeuchte
//Einstellbarer OFFSET für Temperatur -> gemessene Temp +/- Offset = Angezeigte Temp.
#define OFFSETtemp 0 //z.B -50 ≙ -5°C / 50 ≙ +5°C

//Einstellbarer OFFSET für Luftfeuchte -> gemessene Luftf. +/- Offset = Angezeigte Luftf.
#define OFFSEThumi 0 //z.B -10 ≙ -10%RF / 10 ≙ +10%RF

//-----------------------------------------------------------------------------------------

// all library classes are placed in the namespace 'as'
using namespace as;

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xf3, 0xd3, 0x03},     // Device ID
  "SGIAQTST03",           // Device Serial
  {0xf3, 0xd3},           // Device Model Indoor //orig 0xf1d1
  0x10,                   // Firmware Version
  as::DeviceType::THSensor, // Device Type
  {0x01, 0x00}            // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AvrSPI<10, 11, 12, 13> SPIType;
typedef Radio<SPIType, 2> RadioType;
typedef StatusLed<LED_PIN> LedType;
//typedef AskSin<LedType, BatterySensor, RadioType> BaseHal;
typedef AskSin<LedType, BATT_SENSOR, RadioType> BaseHal;
class Hal : public BaseHal {
  public:
    void init (const HMID& id) {
      BaseHal::init(id);
#ifdef USE_CC1101_ALT_FREQ_86835
      // 2165E8 == 868.35 MHz
      radio.initReg(CC1101_FREQ2, 0x21);
      radio.initReg(CC1101_FREQ1, 0x65);
      radio.initReg(CC1101_FREQ0, 0xE8);
#endif
      // measure battery every 1h
      battery.init(seconds2ticks(60UL * 60), sysclock);  // 60UL * 60 for 1hour
      battery.low(18);
      battery.critical(15);
    }

    bool runready () {
      return sysclock.runready() || BaseHal::runready();
    }
} hal;

DEFREGISTER(Reg0, MASTERID_REGS, 0x20, 0x21, 0x22, 0x23)
class SensorList0 : public RegList0<Reg0> {
  public:
    SensorList0(uint16_t addr) : RegList0<Reg0>(addr) {}

    bool updIntervall (uint16_t value) const {
      return this->writeRegister(0x20, (value >> 8) & 0xff) && this->writeRegister(0x21, value & 0xff);
    }
    uint16_t updIntervall () const {
      return (this->readRegister(0x20, 0) << 8) + this->readRegister(0x21, 0);
    }

    bool height (uint16_t value) const {
      return this->writeRegister(0x22, (value >> 8) & 0xff) && this->writeRegister(0x23, value & 0xff);
    }
    uint16_t height () const {
      return (this->readRegister(0x22, 0) << 8) + this->readRegister(0x23, 0);
    }

    void defaults () {
      clear();
      updIntervall(11);
      height(54);
    }
};


class WeatherEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, int16_t temp, uint16_t airPressure, uint8_t humidity, uint8_t iaq_percent, uint8_t iaq_state, bool batlow, uint8_t volt, int16_t ref_temperature, uint8_t ref_humidity, uint16_t ref_airPressure, uint16_t ref_tvoc) {
      uint8_t t1 = (temp >> 8) & 0x7f;
      uint8_t t2 = temp & 0xff;
      if ( batlow == true ) {
        t1 |= 0x80; // set bat low bit
      }
      Message::init(0x18, msgcnt, 0x70, BIDI | WKMEUP, t1, t2);
      pload[0] = (airPressure >> 8) & 0xff;
      pload[1] = airPressure & 0xff;
      pload[2] = humidity & 0xff;
      pload[3] = iaq_percent & 0xff;
      pload[4] = iaq_state & 0xff;
      pload[5] = volt & 0xff;
      pload[6] = (ref_temperature >> 8) & 0xff;
      pload[7] = ref_temperature & 0xff;
      pload[8] = ref_humidity & 0xff;
      pload[9] = (ref_airPressure >> 8) & 0xff;
      pload[10] = ref_airPressure & 0xff; 
      pload[11] = (ref_tvoc >> 8) & 0xff;
      pload[12] = ref_tvoc & 0xff;      
    }
};

class WeatherChannel : public Channel<Hal, List1, EmptyList, List4, PEERS_PER_CHANNEL, SensorList0>, public Alarm {
    WeatherEventMsg msg;
    Sens_Bme680<0x77>   bme680; //SG: changed from default <> to <0x77> for Adafruit sensor 
    Sens_SHT31<0x44>    sht31;  //SG: GY breakout board standard
    uint16_t        millis;

  public:
    WeatherChannel () : Channel(), Alarm(10), millis(0) {}
    virtual ~WeatherChannel () {}

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      uint8_t msgcnt = device().nextcount();
      // reactivate for next measure
      tick = delay();
      clock.add(*this);
      bme680.measure(this->device().getList0().height());
      sht31.measure();
            
      DPRINT("corrected T/H = ");DDEC(bme680.temperature()+OFFSETtemp);DPRINT("/");DDECLN(bme680.humidity()+OFFSEThumi);
      DPRINT("ref temp / hum = ");DDEC(sht31.temperature());DPRINT("/");DDECLN(sht31.humidity());
     
      // msg.init( msgcnt,bme680.temperature()+OFFSETtemp,bme680.pressureNN(),bme680.humidity()+OFFSEThumi,bme680.iaq_percent(), bme680.iaq_state(), device().battery().low(), device().battery().current());
      msg.init( msgcnt,bme680.temperature()+OFFSETtemp,bme680.pressureNN(),bme680.humidity()+OFFSEThumi,bme680.iaq_percent(), bme680.iaq_state(), device().battery().low(), device().battery().current() / 100, sht31.temperature(), sht31.humidity(), 10420, 42);

      if (msgcnt % 10 == 2) device().sendPeerEvent(msg, *this); else device().broadcastEvent(msg, *this);
    }

    uint32_t delay () {
      return seconds2ticks(max(this->device().getList0().updIntervall(),SAMPLINGINTERVALL_IN_SECONDS));
    }
    void setup(Device<Hal, SensorList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);
      bme680.init();
      sht31.init();
      sysclock.add(*this);
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      return 0;
    }
};

class IAQDevice : public MultiChannelDevice<Hal, WeatherChannel, 1, SensorList0> {
  public:
    typedef MultiChannelDevice<Hal, WeatherChannel, 1, SensorList0> TSDevice;
    IAQDevice(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
    virtual ~IAQDevice () {}

    virtual void configChanged () {
      TSDevice::configChanged();
      //DPRINTLN("* Config Changed       : List0");
      //DPRINT(F("* SENDEINTERVALL       : ")); DDECLN(this->getList0().updIntervall());
      //DPRINT(F("* ALTITUDE             : ")); DDECLN(this->getList0().height());
    }
};

IAQDevice sdev(devinfo, 0x20);
ConfigButton<IAQDevice> cfgBtn(sdev);

void setup () {
  //SG: switch on MOSFET to power CC1101
  pinMode(4, OUTPUT);
  digitalWrite (4, LOW);
  
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    hal.activity.savePower<Sleep<>>(hal);
  }
}