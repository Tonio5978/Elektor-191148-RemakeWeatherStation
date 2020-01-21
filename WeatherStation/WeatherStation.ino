//****************************************************************************
//
//                        Elektor Weatherstation 191148 
//
//  Plattform: ESP32 Pico Kit V4.0 or newer
//  
//  Pinmapping
//  IO02 : RES  ( RFM95 )
//  IO14 : SCKL ( RFM95 )
//  IO12 : MISO ( RFM95 )
//  IO13 : MOSI ( RFM95 )
//  IO15 : NSS  ( RFM95 )
//  IO32 : DIO2 ( RFM95 )
//  IO33 : DIO1 ( RFM95 )
//  IO34 : DIO0 ( RFM95 )
//  IO25 : SCL
//  IO26 : SDA
//  IO34 : WIND SPEED
//  IO38 : RAIN
//  IO37 : WIND DIR
//  IO19 : MISO ( SD-CARD )
//  IO23 ; MOSI ( SD-CARD )
//  IO18 : SCKL ( SD-CARD )
//  IO05 : CS0  ( SD-CARD )
//  IO09 : TX1 ( Particle Sensor )
//  IO10 : RX1 ( Particle Sensor )
//
//  ESP32 Arduino Core requiered
//
//  Librarys requiered:
//
//  - LiquidCrystal_I2C ( https://github.com/johnrickman/LiquidCrystal_I2C )
//  - eHaJo Absolut Pressure AddOn by Hannes Jochriem ( https://github.com/ehajo/WSEN-PADS )
//  - LoraWAN-MAC-in-C library ( https://github.com/mcci-catena/arduino-lmic )
//  - Adafruit Unified Sensors
//  - Adafruit BME280 Library
//  - Adafruit VEML6070 Library
//  - Adafruit VEML6075 Library
//  - Adafruit TSL2561 Library
//  - Adafruit TSL2591 Library
//  - Adafruit BusIO
//  - Time by Michael Margolis
//  - AdrduinoJson 6.x 
//  - PubSubclient by Nick o'Leary
//  - NTP Client Lib
//  - OneWire by Paul Stoffregen ( https://github.com/PaulStoffregen/OneWire )
//****************************************************************************
#include <Arduino.h>
#include "FS.h"
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "SPIFFS.h"
#include <Update.h>

#include "./src/lora_wan/lorawan.h"
#include "./src/lora_raw/loraraw.h"

#include "./src/NTPClient/ntp_client.h"

#include "./src/I2C_Sensors/i2c_sensors.h"
#include "./src/uart_pm_sensors/uart_pm_sensors.h"

#include "./src/ValueMapping/ValueMapping.h"
#include "./src/InternalSensors/InternalSensors.h"
#include "./src/TimeCore/timecore.h"
#include "./wifi_net.h"

#include "./src/SenseBox/SenseBox.h"
#include "./src/ThingSpeak/thingspeak.h"

#include "./src/sdcard/sdcard_if.h"

#include "./datastore.h"

#include "webserver_map_fnc.h"
#include "webserver_sensebox_fnc.h"
#include "webserver_thinkspeak_fnc.h"
#include "webserver_time_fnc.h"

#include "./src/lcd_menu/lcd_menu.h"

/* We define the pins used for the various components */

#define INPUT_RAIN           ( 38 )
#define INPUT_WINDSPEED      ( 34 )
#define INPUT_WINDDIR        ( 37 )

#define I2C0_SCL             ( 25 )
#define I2C0_SDA             ( 26 )

#define PARTICLESENSOR_RX    ( 10 )
#define PARTICLESENSOR_TX    (  9 )

//#define SD_MISO              ( 12 )
#define SD_MISO              ( 15 )
#define SD_MOSI              ( 13 )
#define SD_SCK               ( 14 )
//#define SD_CS0               ( 15 )
#define SD_CS0               ( 12 )




#define RFM95_MISO           ( 19 )
#define RFM95_MOSI           ( 23 )
#define RFM95_SCK            ( 18 )
#define RFM95_NSS            (  5 )
#define RFM95_RES            ( 0xFF )
#define RFM95_DIO0           ( 35 )
#define RFM95_DIO1           ( 33 )
#define RFM95_DIO2           ( 32 )

#define USERBTN0             ( 0 )
#define USERBTN1             ( 4 )


 lorawan LORAWAN;


 I2C_Sensors TwoWireSensors(I2C0_SCL, I2C0_SDA );
 UART_PM_Sensors PMSensor( PARTICLESENSOR_RX , PARTICLESENSOR_TX );
 VALUEMAPPING SensorMapping;
 InternalSensors IntSensors;

LiquidCrystal_I2C lcd(0x27,20,4);  // set the LCD address to 0x27 for a 16 chars and 2 line display

Timecore TimeCore;
NTP_Client NTPC;

SenseBoxUpload SenseBox;
ThinkspeakUpload ThinkSpeak;

void DataLoggingTask(void* param);
bool ReadSensorData(float* data ,uint8_t ch);
void SDCardDataLog( void );

void setup_iopins( void ){
  // Every pin that is just IO will be defined here 
  pinMode( USERBTN0, INPUT_PULLUP );
  //If this button is read as 0 this means it is pressed
  if(USERBTN1 >= 0){
    pinMode( USERBTN1, INPUT_PULLUP );
  }

}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(); /* This can be called multiple times, allready mounted it will just return */
  datastoresetup();
  setup_iopins();

  //We also need to mount the SPIFFS
  if(!SPIFFS.begin(true)){
      Serial.println("An Error has occurred while mounting SPIFFS");
  }
  //This will setup the internal sensors
  IntSensors.begin(INPUT_WINDDIR, INPUT_WINDSPEED, INPUT_RAIN );
  /* Next is to collect the I²C-Zoo of supported sensor */
  TwoWireSensors.begin();
  LCDMenu( USERBTN0, USERBTN1 );
  //This sould trigger autodetect
  PMSensor.begin( Serial1 , UART_PM_Sensors::SerialSensorDriver_t::NONE );
  //Next step is to setup wifi and check if the configured AP is in range
  /* We check if the boot button is pressed ( IO00 ) */
  WiFiClientEnable(true); //Force WiFi on 
  WiFiForceAP(false); //Diable Force AP
  if(0 != digitalRead(USERBTN0 )){
    initWiFi( false, false );
  } else {
    Serial.println("Force System to AP");
    initWiFi( false , true );   
  }
  Serial.println("Reduce powerusage for WiFi(10s)");
  for(uint32_t i=0;i<10000;i++){
    vTaskDelay(1);
    NetworkLoopTask();
  } 
  Serial.println("Continue boot");
  if ( false == LORAWAN.begin( RFM95_NSS, 0xFF , 0xFF, RFM95_DIO0, RFM95_DIO1, RFM95_DIO2 ) ){
    //LoRa Module not found, we won't schedule any transmission
  } else {
    //LoRa WAN Enabled
  }
  
  TimeCore.begin(true);
  Webserver_Time_FunctionRegisterTimecore(&TimeCore);
  Webserver_Time_FunctionRegisterNTPClient(&NTPC);


  //We need to register the drivers
  SensorMapping.RegisterInternalSensors(&IntSensors);
  SensorMapping.RegisterI2CBus(&TwoWireSensors);
  SensorMapping.RegisterUartSensors(&PMSensor);
  //
  SensorMapping.begin();
  //We need to setup the Dataloggin parts now
  SenseBox.begin();
  SenseBox.RegisterDataAccess( ReadSensorData );
  Webserver_SenseBox_RegisterSensebox(&SenseBox);

  ThinkSpeak.begin(false); //We need to stick to HTTP
  ThinkSpeak.RegisterDataAccess( ReadSensorData );
  Webserver_Thinkspeak_RegisterThinkspeak(&ThinkSpeak);
  //Next is the SD-Card access, and this is a bit tricky
  SDCardRegisterMappingAccess(&SensorMapping);
  SDCardRegisterTimecore(&TimeCore);
  //As also other parts use the card, if arround to log data

  //Last will be the MQTT Part for now
  setup_sdcard(SD_SCK ,SD_MISO, SD_MOSI, SD_CS0 );
  sdcard_mount();
  
  
    xTaskCreatePinnedToCore(
                    DataLoggingTask,   /* Function to implement the task */
                    "DataLogging",  /* Name of the task */
                    16000,       /* Stack size in words */
                    NULL,       /* Task input parameter */
                    5,          /* Priority of the task */
                    NULL,  /* Task handle. */
                    0);         /* Core where the task should run */

     xTaskCreatePinnedToCore(
      NTP_Task,       /* Function to implement the task */
      "NTP_Task",  /* Name of the task */
      10000,          /* Stack size in words */
      NULL,           /* Task input parameter */
      1,              /* Priority of the task */
      NULL,           /* Task handle. */
      1); 


 sdcard_log_int( 0 );
 sdcard_log_enable( true );


 
}


bool ReadSensorData(float* data ,uint8_t ch){
  return SensorMapping.ReadMappedValue(data,ch);
}



void loop() {
  float value =0;
  static uint32_t last_ms = 0;
  uint32_t milli = millis();
  /* This will be executed in the arduino main loop */
  LORAWAN.LoRaWAN_Task();
  //The Rain and WindSensors are now processed by its own tasks
  //We can now take care for the important stuff
  if( (milli - last_ms) > 1000 ){
    last_ms = milli;
    //Once a second to be called
    
  }
  NetworkLoopTask();
}


/* Datalogging intervall can for the network only set global */
/* This is a generic test function */
void DataLoggingTask(void* param){
  //We will force a mapping for the internal sensors here , just for testing

  


  //This will grab every 10 seconds new values from the mapped sensors and display them as test
  while(1==1){
    for(uint8_t i=0;i<64;i++){
      float value = NAN;
      if(false == SensorMapping.ReadMappedValue(&value,i)){
        //Serial.printf("Channel %i not mapped\n\r",i);
      } else {
        Serial.printf("Channel %i Value %f",i,value );
        Serial.print(" @ ");
        String name = SensorMapping.GetSensorNameByChannel(i);
        Serial.print(name);
      }

    }
    Serial.println("");
  vTaskDelay(60000); //60s delay
  }
}


void NTP_Task( void * param){
  Serial.println(F("Start NTP Task now"));
  NTPC.ReadSettings();
  NTPC.begin( &TimeCore );
  NTPC.Sync();

  /* As we are in a sperate thread we can run in an endless loop */
  while( 1==1 ){
    /* This will send the Task to sleep for one second */
    vTaskDelay( 1000 / portTICK_PERIOD_MS );  
    NTPC.Tick();
    NTPC.Task();
  }
}