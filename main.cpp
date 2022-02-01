#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GrayOLED.h>
#include <Adafruit_SPITFT.h>
#include <Adafruit_SPITFT_Macros.h>
#include "Adafruit_SSD1306.h"
#include <gfxfont.h>
#include <PCA9539.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <WiFi.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#ifdef __cplusplus
extern "C"
{
#endif

  uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

void mainTask(void *p);
double getMultiplicator();
void setChannel(int newChannel);
void wifi_promiscuous(void *buf, wifi_promiscuous_pkt_type_t type);
void updateDisplay();
String hostname = "";
/* InfluxDB Connection Parameters */
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define INFLUXDB_URL ""
// InfluxDB v2 server or cloud API authentication token (Use: InfluxDB UI -> Data -> Tokens -> <select token>)
#define INFLUXDB_TOKEN ""
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG ""
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET ""
#define TZ_INFO ""
Point DEFENDER("DEFENDER");

#define MAX_CH 13     // 1 - 14 channels (1-11 for US, 1-13 for EU and 1-14 for Japan)
#define SNAP_LEN 2324 // max len of each recieved packet

#define SDA_PIN 19
#define SCL_PIN 18
#define MAX_X 128
#define MAX_Y 51

PCA9539 ledport(0x27);
Adafruit_SSD1306 display(128, 64, &Wire, 4);
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

TimerHandle_t ifdbTimer;
TimerHandle_t postTimer;
TimerHandle_t chTimer;

uint32_t tmp;
uint32_t pkts[MAX_X]; // here the packets per second will be saved
uint32_t deauths = 0; // deauth frames per second
uint32_t tlpkts = 0;
bool deauth_ch[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint32_t ch_pkt[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint32_t pps = 0;
bool ifdb = false;
bool post = false;
bool sw = false;
bool is_deauth = false;
unsigned int ch = 1;
int rssi;

void vTimerCallback1(TimerHandle_t ifdbTimer)
{
  ifdb = true;
}
void vTimerCallback2(TimerHandle_t postTimer)
{
  post = true;
}
void vTimerCallback3(TimerHandle_t chTimer)
{
  sw = true;
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
  return ESP_OK;
}

double getMultiplicator()
{
  uint32_t maxVal = 1;
  for (int i = 0; i < MAX_X; i++)
  {
    if (pkts[i] > maxVal)
      maxVal = pkts[i];
  }
  if (maxVal > MAX_Y)
    return (double)MAX_Y / (double)maxVal;
  else
    return 1;
}

void setChannel(int newChannel)
{
  ch = newChannel;
  if (ch > MAX_CH || ch < 1)
    ch = 1;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);
  esp_wifi_start();
}

void wifi_promiscuous(void *buf, wifi_promiscuous_pkt_type_t type)
{
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;

  if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0))
  {
    if (deauths == 1)
    {
      deauth_ch[ch - 2] = true;
    }
    deauths++;
  }
  if (deauths == 0)
  {
    deauth_ch[ch - 2] = false;
  }
  // ledport.digitalWrite(14, HIGH);
  if (type == WIFI_PKT_MISC)
  {
  }
  //ledport.digitalWrite(14, HIGH);
  if (ctrl.sig_len > SNAP_LEN)
  {
  }
  //ledport.digitalWrite(14, HIGH);

  uint32_t packetLength = ctrl.sig_len;
  if (type == WIFI_PKT_MGMT)
    packetLength -= 4;
  tmp++;
  rssi == ctrl.rssi;
}

void updateDisplay()
{
  double multiplicator = getMultiplicator();
  int len;
  int rssi;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("CH");
  display.setCursor(12, 0);
  display.print(ch);
  display.setCursor(35, 0);
  display.print(rssi);
  display.setCursor(65, 0);
  display.print(deauths);
  display.setCursor(80, 0);
  if (tlpkts > 1000000000)
  {
    String tlpktsres = (String)(tlpkts / 1000000000) + "." + (String)((tlpkts % 1000000000) / 100000000) + "B";
    display.print(tlpktsres);
  }
  else if (tlpkts > 1000000)
  {
    String tlpktsres = (String)(tlpkts / 1000000) + "." + (String)((tlpkts % 1000000) / 100000) + "M";
    display.print(tlpktsres);
  }
  else if (tlpkts > 1000)
  {
    String tlpktsres = (String)(tlpkts / 1000) + "." + (String)((tlpkts % 1000) / 100) + "k";
    display.print(tlpktsres);
  }
  else
  {
    display.print(tlpkts);
  }

  display.drawLine(0, 63 - MAX_Y, MAX_X, 63 - MAX_Y, WHITE);
  for (int i = 0; i < MAX_X; i++)
  {
    len = pkts[i] * multiplicator;
    display.drawLine(i, 63, i, 63 - (len > MAX_Y ? MAX_Y : len), WHITE);

    if (i < MAX_X - 1)
      pkts[i] = pkts[i + 1];
  }
  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.setCursor(45, 45);
  display.print(pps);
  display.display();
}

void setup()
{

  Serial.begin(115200);
  Wire.begin(19, 18, 400000);
  delay(150);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
  delay(150);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str()); //define hostname
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[INIT] ERROR CONNECTING TO WIFI");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    vTaskDelay(2000);
  }
  client.setHTTPOptions(HTTPOptions().httpReadTimeout(200));
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  // Check server connection
  if (client.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
  Serial.println("[INIT] IFDB CONNECTION OK");
  tcpip_adapter_init();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  xTaskCreatePinnedToCore(
      mainTask,   /* Function to implement the task */
      "mainTask", /* Name of the task */
      50000,      /* Stack size in words */
      NULL,       /* Task input parameter */
      1,          /* Priority of the task */
      NULL,       /* Task handle. */
      0);         /* Core where the task should run */

  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);
  ifdbTimer = xTimerCreate("Timer1", 5000, pdTRUE, (void *)0, vTimerCallback1);
  xTimerStart(ifdbTimer, 0);
  postTimer = xTimerCreate("Timer2", 1000, pdTRUE, (void *)0, vTimerCallback2);
  xTimerStart(postTimer, 0);
  chTimer = xTimerCreate("Timer3", 50, pdTRUE, (void *)0, vTimerCallback3);
  xTimerStart(chTimer, 0);
}

void loop()
{
}

void mainTask(void *p)
{
  for (int i = 0; i < 16; i++)
  {
    ledport.pinMode(i, OUTPUT);
    ledport.digitalWrite(i, HIGH);
  }
  delay(500);
  for (int i = 0; i < 13; i++)
  {
    ledport.digitalWrite(i, LOW);
  }
  while (true)
  {
    if (post)
    {
      for (int i = 0; i < 13; i++)
      {
        pps += ch_pkt[i];
      }
      pkts[MAX_X - 1] = (uint32_t)pps;
      updateDisplay();
      if (deauths >= 1)
      {
        ledport.digitalWrite(14, HIGH);
        is_deauth = true;
      }
      else
      {
        ledport.digitalWrite(14, LOW);
        is_deauth = false;
      }
      deauths = 0;
      post = false;
    }
    if (sw)
    {

      if (deauth_ch[ch - 2] == 0)
      {

        ledport.digitalWrite((uint8_t)ch - 2, LOW);
      }
      ch_pkt[ch - 2] = tmp * 20;
      tlpkts += tmp * 20;
      ch++;
      tmp = 0;
      ledport.digitalWrite((uint8_t)ch - 2, HIGH);
      if (ch > MAX_CH + 1)
      {
        ch = 1;
      }
      sw = false;
      ledport.digitalWrite((uint8_t)13, !ledport.digitalRead(13));
    }

    if (ifdb)
    {
      DEFENDER.clearFields();
      DEFENDER.clearTags();
      DEFENDER.addTag("UID", "N/A");
      DEFENDER.addField("Total Pkts", tlpkts);
      DEFENDER.addField("Total PPS", pps);
      DEFENDER.addField("Deauth Status", is_deauth);
      DEFENDER.addField("CPU Temperature", ((temprature_sens_read() - 32) / 1.8));
      DEFENDER.addField("Deauths PPS", deauths);
      DEFENDER.addField("CH1 PPS", (ch_pkt[0]));
      DEFENDER.addField("CH2 PPS", (ch_pkt[1]));
      DEFENDER.addField("CH3 PPS", (ch_pkt[2]));
      DEFENDER.addField("CH4 PPS", (ch_pkt[3]));
      DEFENDER.addField("CH5 PPS", (ch_pkt[4]));
      DEFENDER.addField("CH6 PPS", (ch_pkt[5]));
      DEFENDER.addField("CH7 PPS", (ch_pkt[6]));
      DEFENDER.addField("CH8 PPS", (ch_pkt[7]));
      DEFENDER.addField("CH9 PPS", (ch_pkt[8]));
      DEFENDER.addField("CH10 PPS", (ch_pkt[9]));
      DEFENDER.addField("CH11 PPS", (ch_pkt[10]));
      DEFENDER.addField("CH12 PPS", (ch_pkt[11]));
      DEFENDER.addField("CH13 PPS", (ch_pkt[12]));

      if (!client.writePoint(DEFENDER))
      {
        Serial.println("FAILED");
      }
      ifdb = false;
    }
  }
}
