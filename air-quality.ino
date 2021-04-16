#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Ticker.h>
#include <uFire_SHT20.h>
//#include "iAQCoreI2C.h"
#include "PMS.h"
#include "iAQcore.h"

uFire_SHT20 sht20;
//iAQCoreI2C iaq;
iAQcore iaqcore;

#define SCL 5
#define SDA 4

#ifndef STASSID
#define STASSID "FudanNet_2G"
#define STAPSK  "Fud@n$Net8"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

const char* host = "192.168.100.54";
const uint16_t port = 6666;

typedef unsigned char u8;
typedef unsigned short u16;

u16 rxcout1 = 0;

u8 mySerial_rxbuf[256];
int yline = 1;
unsigned long time1;

uint16_t eco2;
uint16_t stat;
uint32_t resist;
uint16_t etvoc;



//U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);
U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0,  /*SCL*/  SCL,  /*SDA*/  SDA,   /*reset*/  U8X8_PIN_NONE);
SoftwareSerial *mySerial = new SoftwareSerial(2, 2);
SoftwareSerial *mySerial1 = new SoftwareSerial(12, 12);
Ticker dispRefresh, AmbMeasure;

PMS pms(*mySerial1);
PMS::DATA data;

struct myAmbient_t {
  float mytemp;
  float myhum;
  u16 tvocCo;
  u16 pm25;
} myambient;

struct CH2O_T {
  u8 chName;  //1
  u8 chUnit;  //2
  u16 chCo;
  u16 chFull; //6
} CH2O;

#define MYLINEPIX 15

void disprfsh()
{
  //Serial.println("Display Refresh");
  u8g2.clearBuffer();          // clear the internal memory
  /*u8g2.setFont(u8g2_font_unifont_t_chinese2);
    u8g2.setCursor(0, yline*14);
    u8g2.print("我的地址:");
    u8g2.setFont(u8g2_font_ncenB10_tr); // choose a suitable font
    u8g2.drawStr(0,(yline+1)*14,(const char*)WiFi.localIP().toString().c_str());
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(110,(yline+1)*14,"ppm");*/
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, MYLINEPIX);
  u8g2.println("温度°C  湿度  甲醛ppb");
  u8g2.setCursor(0, MYLINEPIX * 2);
  u8g2.println((String)sht20.tempC + "  " + sht20.RH + "%  " + (String)CH2O.chCo);
  u8g2.setCursor(0, MYLINEPIX * 3);
  u8g2.printf("TVOC: %d ppb", etvoc);
  u8g2.setCursor(0, MYLINEPIX * 4);
  u8g2.println("PM2.5: " + (String)data.PM_AE_UG_2_5 + " ug/m3");
  u8g2.sendBuffer();          // transfer internal memory to the display
}

void funserRx()
{
  myCH2O();
  myPms();
}

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(9600);
  String str = "Hello NodeMCU!";
  mySerial->begin(9600);
  mySerial1->begin(9600);
  /*wifi OTA*/
  //Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  /* OLED 0.5Hz */
  u8g2.begin();
  u8g2.enableUTF8Print();
  dispRefresh.attach(2.0, disprfsh);
  /* sht20温湿度 */
  sht20.begin();
  /* TVOC */
  Wire.begin(/*SDA*/SDA,/*SCL*/SCL);
  Wire.setClockStretchLimit(1000);
  iaqcore.begin();
  /* 串口线程 1000Hz*/
  AmbMeasure.attach_ms(1, funserRx);
}

void loop() {
  // put your main code here, to run repeatedly:
  ArduinoOTA.handle();
  debugAmbient();
  delay(3000);
  myTVOC();
  // Wait
  delay(1000);
}

bool T_host(char* upbuf)
{
  WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    delay(5000);
    return false;
  }
  if (client.connected()) {
    client.println(upbuf);
  }
  client.stop();
  return true;
}

void funcCH2O(u8 *data, u8 ln)
{
  static unsigned long timenow = 0, timebefore = 0;
  if ((*data == 0xFF)
      && (*(data + 8) == FucCheckSum(data, ln)))
  {
    rxcout1 = 0;
    CH2O.chName = *(data + 1);
    CH2O.chUnit = *(data + 2);
    CH2O.chCo = (*(data + 4)) * 256 + *(data + 5);
    CH2O.chFull = (*(data + 6)) * 256 + *(data + 7);
    timenow = millis();
    if (timenow-timebefore>10000)
    {
      for (int i = 0; i < 9; i++)
      {
        Serial.printf("%02X ", *(data + i));
      }
      Serial.println("");
      Serial.printf("checksum = 0x%02X\n", FucCheckSum(data, ln));
      Serial.printf("chName = 0x%02X\n", CH2O.chName);
      Serial.printf("chUnit = 0x%02X\n", CH2O.chUnit);
      Serial.printf("chCo = %d\n", CH2O.chCo);
      Serial.printf("chFull = %d\n", CH2O.chFull);
      timebefore = timenow;
    }
    return;
  }
  rxcout1 = 0;
}

unsigned char FucCheckSum(unsigned char *i, unsigned char ln)
{
  unsigned char j, tempq = 0;
  i += 1;
  for (j = 0; j < (ln - 2); j++)
  {
    tempq += *i;
    i++;
  }
  tempq = (~tempq) + 1;
  return (tempq);
}

void debugAmbient()
{
  sht20.measure_all();
  Serial.println((String)sht20.tempC + "°C");
  Serial.println((String)sht20.tempF + "°F");
  Serial.println((String)sht20.dew_pointC + "°C dew point");
  Serial.println((String)sht20.dew_pointF + "°F dew point");
  Serial.println((String)sht20.RH + " %RH");
  Serial.println((String)sht20.vpd() + " kPa VPD");
  Serial.println();
}

void myTVOC()
{
  // Read
  iaqcore.read(&eco2, &stat, &resist, &etvoc);
  // Print
  Serial.print("iAQcore: ");
  Serial.print("eco2=");   Serial.print(eco2);     Serial.print(" ppm,  ");
  Serial.print("stat=0x"); Serial.print(stat, HEX); Serial.print(",  ");
  Serial.print("resist="); Serial.print(resist);   Serial.print(" ohm,  ");
  Serial.print("tvoc=");   Serial.print(etvoc);    Serial.print(" ppb");
  Serial.println();
}

void myCH2O()
{
  if (mySerial->available())
  {
    mySerial_rxbuf[rxcout1++ % 256] = mySerial->read();
    if (mySerial_rxbuf[0] != 0xff)
      rxcout1 = 0;
  }
  if (rxcout1 == 9)
  {
    funcCH2O(mySerial_rxbuf, 9);
  }
}

void myPms()
{
  if (pms.read(data))
  {
    static unsigned long timenow = 0, timebefore = 0;
    timenow = millis();
    if (timenow-timebefore>10000)
    {
      Serial.print("PM 1.0 (ug/m3): ");
      Serial.println(data.PM_AE_UG_1_0);

      Serial.print("PM 2.5 (ug/m3): ");
      Serial.println(data.PM_AE_UG_2_5);

      Serial.print("PM 10.0 (ug/m3): ");
      Serial.println(data.PM_AE_UG_10_0);

      Serial.println(); 
      timebefore = timenow;
    }
  }
}

/*
  if(mySerial_rxbuf[rxcout1-1] == '\n')
  {
  mySerial_rxbuf[rxcout1-1] = '\n';
  mySerial_rxbuf[rxcout1] = 0;
  Serial.printf("last[%d]:%02X %02X\n",rxcout1,mySerial_rxbuf[rxcout1-2],mySerial_rxbuf[rxcout1-1]);
  rxcout1 = 0;
  Serial.println((char*)mySerial_rxbuf);
  T_host((char*)mySerial_rxbuf);
  }*/
