#include <Arduino.h>
#include "base64.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include <nvs_flash.h>
#include <HardwareSerial.h>    //导入ESP32串口操作库,使用这个库我们可以把串口映射到其他的引脚上使用
#include <ArduinoJson.h>
/* 各个函数作用（其中4、5懒得写了，抱歉哈）
1、串口：解析串口助手发送的wifi信息
2、时间：获取时间服务器的数据
3、天气：获取心知天气的数据、包括温度、天气
4、模块：dht11测量温湿度
5、显示：OLED
 */

HTTPClient http_client;
esp_err_t err;
unsigned short i;
HardwareSerial MySerial_esp32(1); 
uint8_t txValue = 0,WIFI_flag=0,mode_flag=1;
char WIFISSID[20],WIFIPSID[20];

char wifi_ssid[20] = { 0 };
char wifi_passwd[65] = { 0 };
uint32_t wifi_update = 0;
size_t len;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;
void read_usart();
void printLocalTime();
void UART_WiFi(std::string WiFi_Data);
void httpRequest();
void parseJson(WiFiClient client);
hw_timer_t * timer = NULL;
const int httpPort = 80; //端口号
const char* host = "api.seniverse.com"; //服务器地址
String reqUserKey = "xxxxxxxxx";//知心天气API私钥  -----换成你的
String reqLocation = "天津";//地址-----随意修改
String reqUnit = "c";//摄氏度
//-------------------http请求-----------------------------//
String reqRes = "/v3/weather/now.json?key=" + reqUserKey +
                + "&location=" + reqLocation +
                "&language=en&unit=" + reqUnit;
String httprequest = String("GET ") + reqRes + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n\r\n";
//--------------------------------------------------------//

void setup() {
    Serial.begin(115200);
    MySerial_esp32.begin(115200, SERIAL_8N1, 14, 15); 
    uint8_t count=0;
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS分区被截断，需要删除,然后重新初始化NVS */
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    /* 定义一个NVS操作句柄 */
    nvs_handle my_HandleNvs;
    /* 打开一个NVS命名空间 */
    err = nvs_open("WiFi_cfg", NVS_READWRITE, &my_HandleNvs);
    nvs_get_u32(my_HandleNvs,"wifi_update",&wifi_update);
    nvs_commit(my_HandleNvs);
    nvs_close(my_HandleNvs);
    if(wifi_update==1){//wifi_update为1说明WIFI信息已保存 
        nvs_open("WiFi_cfg", NVS_READWRITE, &my_HandleNvs);
        len = sizeof(wifi_ssid);
        nvs_get_str(my_HandleNvs,"wifi_ssid",wifi_ssid,&len);   
        len = sizeof(wifi_passwd);
        nvs_get_str(my_HandleNvs,"wifi_pswd",wifi_passwd,&len); 
        nvs_commit(my_HandleNvs);
        nvs_close(my_HandleNvs);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);//开启网络 
        WiFi.begin(wifi_ssid,wifi_passwd);//填写自己的wifi账号密码
        while (WiFi.status() != WL_CONNECTED) 
        {
            Serial.print(".");
            count++;
            if(count>=200){
                Serial.printf("\r\n-- wifi connect fail! --");
                break;
            }
            vTaskDelay(200);
        }
        Serial.printf("\r\n-- wifi connect success! --\r\n");
     }
    while(wifi_update==0)
    {
        i = MySerial_esp32.available();  //返回目前串口接收区内的已经接受的数据量
        char temp[i];  
        u8_t ConnectCnt = 0;
        if(i != 0){
                while(ConnectCnt<i){
                temp[ConnectCnt] = MySerial_esp32.read();   //读取一个数据并且将它从缓存区删除
                ConnectCnt++;
                }
                MySerial_esp32.println(temp); // 格式化输出
            UART_WiFi(temp);
        }
    }
    //从网络时间服务器上获取并设置时间
    //获取成功后芯片会使用RTC时钟保持时间的更新
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
    httpRequest();
}


uint32_t time1,time2;
void loop() {
    //printLocalTime();
    delay(10);
   
}

int currentHour = 0;                 //时
int currentMinute = 0;               //分
int currentSec = 0;                  //秒
void printLocalTime()
{
  struct tm timeinfo;
  unsigned char testcode[4];
  testcode[0]=0XF4;
  testcode[1]=currentHour;
  testcode[2]=0XF5;
  testcode[3]=currentMinute;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  currentHour=timeinfo.tm_hour;
  currentMinute=timeinfo.tm_min;
  currentSec=timeinfo.tm_sec;
  //Serial.println(timezone);
  //Serial.println(currentHour); // 格式化输出
}
void UART_WiFi(std::string WiFi_Data)
{
    
    u8_t Length = WiFi_Data.length();
    u8_t Interval = 0, ConnectCnt = 0;

    /* 定义一个NVS操作句柄 */
    nvs_handle my_HandleNvs;
    /* 打开一个NVS命名空间 */
    err = nvs_open("WiFi_cfg", NVS_READWRITE, &my_HandleNvs);
    // 首先找到间隔'#'，确定SSID和PSWD的长度
    for (u8_t i = 0; i < Length; i++)
    {
        if ('#' == WiFi_Data[i])
        {
            Interval = i;
        }
    }
    // 首先获取SSID
    std::string Temp = "";
    for (u8_t i = 0; i < Interval - 1; i++)
    {
        Temp += WiFi_Data[i + 1];
    }
    char *SSID = new char[Interval - 1];
    strcpy(SSID, Temp.c_str());
    Serial.printf("SSID:%s\r\n", SSID);
    // 然后才是PSWD
    Temp = "";
    for (u8_t i = 0; i < Length - Interval ; i++)
    {
        Temp += WiFi_Data[i + 1 + Interval];
    }
    char *PSWD = new char[Length - Interval ];
    strcpy(PSWD, Temp.c_str());
    Serial.printf("PSWD:%s\r\n", PSWD);
    err = nvs_set_str(my_HandleNvs,"wifi_ssid",SSID);
    err = nvs_set_str(my_HandleNvs,"wifi_pswd",PSWD);
    len = sizeof(wifi_ssid);
    nvs_get_str(my_HandleNvs,"wifi_ssid",wifi_ssid,&len);   
    len = sizeof(wifi_passwd);
    nvs_get_str(my_HandleNvs,"wifi_pswd",wifi_passwd,&len);   
    // 准备连接Wi-Fi
    if(WIFI_flag==0 ){
        Serial.printf("Connectingto WiFi.");
        WiFi.mode(WIFI_STA);//开启网络 
        WiFi.begin(SSID, PSWD);
        ConnectCnt = 0;
        while (WL_CONNECTED != WiFi.status())
        {
            delay(1000);
            Serial.printf(".");
            if ((++ConnectCnt > 10) || (WL_CONNECTED == WiFi.status()))
            {
                break;
            }
        }

    nvs_set_u32(my_HandleNvs,"wifi_update",1);
    nvs_commit(my_HandleNvs);
    nvs_close(my_HandleNvs);
    // 判断是否连接成功
    if (WL_CONNECTED == WiFi.status())
    {
        Serial.printf("Connected to the WiFi network\r\n");
        Serial.printf("IP address: ");
        Serial.println(WiFi.localIP());
        WIFI_flag=1;
    }
    else
    {
        Serial.print("\r\nFile to connected WiFi\r\n");
    }
}
}
void parseJson(WiFiClient client) {
  const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 2 * JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6) + 230;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, client);

  JsonObject obj1 = doc["results"][0];
  String cityName = obj1["location"]["name"].as<String>();
  String weather = obj1["now"]["text"].as<String>();
  String code = obj1["now"]["code"].as<String>();
  String temperature = obj1["now"]["temperature"].as<String>()+"℃";
  String wind = obj1["now"]["wind_scale"].as<String>();
  String zf= "天津";
  int code_int = obj1["now"]["code"].as<int>();
  Serial.println(cityName);
  Serial.println(wind);
  Serial.println(weather);
  Serial.println(temperature);
  const char *character = zf.c_str(); 
  const char *character_temperature = temperature.c_str(); 
}
void httpRequest() {
  WiFiClient client;
  //1 连接服务器
  if (client.connect(host, httpPort)) {
    Serial.println("连接成功，接下来发送请求");
    client.print(httprequest);//访问API接口
    String response_status = client.readStringUntil('\n');
    Serial.println(response_status);

    if (client.find("\r\n\r\n")) {
      Serial.println("响应报文体找到，开始解析");
    }
    parseJson(client);
  }
  else {
    Serial.println("连接服务器失败");
  }
  client.stop();
}
