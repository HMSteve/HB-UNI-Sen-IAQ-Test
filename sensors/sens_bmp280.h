//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2018-04-03 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

#ifndef __SENSORS_BMP280_h__
#define __SENSORS_BMP280_h__

#include <Sensors.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

namespace as {

// https://github.com/adafruit/Adafruit_BMP280_Library
class Sens_Bmp280 : public Temperature, public Pressure {
  Adafruit_BMP280 _bmp;
public:
  Bmp280 () {}
  void init () {
    _present = _bmp.begin();

//    if(_present == true) {
//      _bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
//                Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
//                Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
//                Adafruit_BMP280::FILTER_X16,      /* Filtering. */
//                Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
//      }

    }


  void measure (__attribute__((unused)) bool async=false) {
    if( present() == true ) {
      _temperature = _bmp.readTemperature() * 10;
      _pressure = _bmp.readPressure() / 100;
    }
  }
};

}

#endif
