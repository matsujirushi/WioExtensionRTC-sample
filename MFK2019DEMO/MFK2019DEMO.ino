// Platform
//  SeeedJP STM32 Boards     ... http://www.seeed.co.jp/package_SeeedJP_index.json
// Libraries
//  Wio cell lib for Arduino ... https://github.com/SeeedJP/Wio_cell_lib_for_Arduino
//  MjGrove                  ... https://github.com/matsujirushi/MjGrove

#include <MjGrove.h>

////////////////////////////////////////////////////////////////////////////////
// Defines

#define BOOT_INTERVAL       (15)    // [sec.]
#define AVERAGE_NUMBER      (40)
#define SYSTEM_RESET_DELAY  (10000) // [msec.]
#define RECEIVE_TIMEOUT     (10000) // [msec.]

#define LED_NONE     0,  0,  0
#define LED_SETUP    0, 10,  0
#define LED_GATHER  10,  0, 10
#define LED_SEND     0,  0, 10
#define LED_STOP    10,  0,  0

#define ERROR()   Error(__FILE__, __LINE__)

////////////////////////////////////////////////////////////////////////////////
// Structs

struct StoreData
{
  byte Count;
  float TemperatureSum;
  float TemperatureMin;
  float TemperatureMax;
  float HumiditySum;
  float HumidityMin;
  float HumidityMax;
};

struct GatherData
{
  float Temperature;
  float Humidity;
};

////////////////////////////////////////////////////////////////////////////////
// Global variables

WioCellular Wio;

GroveBoard Board;
WioExtRTC ExtRTC(&Board.I2C);
GroveTempHumiDHT11 TempHumi(&Board.D38);

////////////////////////////////////////////////////////////////////////////////
// setup and loop

void setup()
{
  delay(200);

  SerialUSB.begin(115200);
  SerialUSB.println();
  SerialUSB.println("--- START ---------------------------------------------------");

  ////////////////////////////////////////
  // Low-level initialize

  SerialUSB.println("### Low-level initialize.");
  Wio.Init();
  Wio.PowerSupplyLed(true);
  Wio.PowerSupplyGrove(true);
  Wio.PowerSupplyCellular(true);
  delay(500);
  
  ////////////////////////////////////////
  // Interface and Device initialize
  
  SerialUSB.println("### Interface and Device initialize.");
  Wio.LedSetRGB(LED_SETUP);
  // I2C
  Board.I2C.Enable();
  if (!ExtRTC.Init()) ERROR();
  // D38
  Board.D38.Enable();
  delay(1000);
  TempHumi.Init();
  Wio.LedSetRGB(LED_NONE);
}

void loop()
{
  ////////////////////////////////////////
  // Read store data
  
  SerialUSB.println("### Read store data.");
  struct StoreData storeData;
  ReadStoreData(&storeData);

  ////////////////////////////////////////
  // Gather data

  SerialUSB.println("### Gather data.");
  Wio.LedSetRGB(LED_GATHER);
  struct GatherData gatherData;
  GatherData(&gatherData);
  Wio.LedSetRGB(LED_NONE);

  SerialUSB.print("temperature = ");
  SerialUSB.print(gatherData.Temperature, 0);
  SerialUSB.println("C");
  SerialUSB.print("humidity = ");
  SerialUSB.print(gatherData.Humidity, 0);
  SerialUSB.println("%");

  ////////////////////////////////////////
  // Update store data

  SerialUSB.println("### Update store data.");
  if (storeData.Count == 0)
  {
    storeData.TemperatureSum = gatherData.Temperature;
    storeData.TemperatureMin = gatherData.Temperature;
    storeData.TemperatureMax = gatherData.Temperature;
    storeData.HumiditySum = gatherData.Humidity;
    storeData.HumidityMin = gatherData.Humidity;
    storeData.HumidityMax = gatherData.Humidity;
  }
  else
  {
    storeData.TemperatureSum += gatherData.Temperature;
    storeData.TemperatureMin = min(storeData.TemperatureMin, gatherData.Temperature);
    storeData.TemperatureMax = max(storeData.TemperatureMax, gatherData.Temperature);
    storeData.HumiditySum += gatherData.Humidity;
    storeData.HumidityMin = min(storeData.HumidityMin, gatherData.Humidity);
    storeData.HumidityMax = max(storeData.HumidityMax, gatherData.Humidity);
  }
  storeData.Count++;
  
  SerialUSB.print("store count = ");
  SerialUSB.println(storeData.Count);
  SerialUSB.print("store temperature = ");
  SerialUSB.print(storeData.TemperatureSum / storeData.Count);
  SerialUSB.print(" , ");
  SerialUSB.print(storeData.TemperatureMin);
  SerialUSB.print(" , ");
  SerialUSB.println(storeData.TemperatureMax);
  
  SerialUSB.print("store humidity = ");
  SerialUSB.print(storeData.HumiditySum / storeData.Count);
  SerialUSB.print(" , ");
  SerialUSB.print(storeData.HumidityMin);
  SerialUSB.print(" , ");
  SerialUSB.println(storeData.HumidityMax);

  ////////////////////////////////////////
  // Send data

  if (storeData.Count >= AVERAGE_NUMBER)
  {
    SerialUSB.println("### Send data.");
    Wio.LedSetRGB(LED_SEND);
    SendData(storeData);
    Wio.LedSetRGB(LED_NONE);
  }

  ////////////////////////////////////////
  // Write store data
  
  SerialUSB.println("### Write store data.");
  WriteStoreData(storeData);

  ////////////////////////////////////////
  // Shutdown
  
  SerialUSB.println("### Shutdown.");
  ExtRTC.SetWakeupPeriod(BOOT_INTERVAL);
  ExtRTC.Shutdown();
  SystemReset();
}

void ReadStoreData(struct StoreData* data)
{
  if (!ExtRTC.Eeprom.Read(0, data, sizeof(*data))) ERROR();
  if (data->Count >= AVERAGE_NUMBER) memset(data, 0, sizeof(*data));
}

void WriteStoreData(const struct StoreData& data)
{
  ExtRTC.Eeprom.Write(0, &data, sizeof(data));
}

void GatherData(struct GatherData* data)
{
  TempHumi.Read();
  data->Temperature = TempHumi.Temperature;
  data->Humidity = TempHumi.Humidity;
}

void SendData(const struct StoreData& data)
{
  SerialUSB.println("Turn on or reset.");
#ifdef ARDUINO_WIO_LTE_M1NB1_BG96
  Wio.SetAccessTechnology(WioCellular::ACCESS_TECHNOLOGY_LTE_M1);
  Wio.SetSelectNetwork(WioCellular::SELECT_NETWORK_MODE_MANUAL_IMSI);
#endif
  if (!Wio.TurnOnOrReset()) ERROR();
  
  SerialUSB.println("Connecting to \"soracom.io\".");
  if (!Wio.Activate("soracom.io", "sora", "sora")) ERROR();
  
  SerialUSB.println("Open.");
  auto connectId = Wio.SocketOpen("uni.soracom.io", 23080, WIO_UDP);
  if (connectId < 0) ERROR();
  
  SerialUSB.println("Send.");
  uint16_t sendData[6];
  sendData[0] = data.TemperatureSum / data.Count * 100;
  sendData[1] = data.TemperatureMin * 100;
  sendData[2] = data.TemperatureMax * 100;
  sendData[3] = data.HumiditySum / data.Count * 100;
  sendData[4] = data.HumidityMin * 100;
  sendData[5] = data.HumidityMax * 100;
  if (!Wio.SocketSend(connectId, (const byte*)sendData, sizeof(sendData))) ERROR();

  SerialUSB.println("Receive.");
  char recvData[100];
  auto length = Wio.SocketReceive(connectId, recvData, sizeof (recvData), RECEIVE_TIMEOUT);
  if (length < 0) ERROR();
  if (length == 0) ERROR();
  SerialUSB.println(recvData);

  SerialUSB.println("Cleanup.");
  if (!Wio.SocketClose(connectId)) ERROR();
  if (!Wio.Deactivate()) ERROR();
  if (!Wio.TurnOff()) ERROR();
}

////////////////////////////////////////////////////////////////////////////////
// Helper functions

void Error(const char* file, int line)
{
  Wio.LedSetRGB(LED_STOP);
  
  const char* filename = strrchr(file, '\\');
  filename = filename ? filename + 1 : file;
  
  SerialUSB.print("ERROR at ");
  SerialUSB.print(filename);
  SerialUSB.print("(");
  SerialUSB.print(line);
  SerialUSB.print(")");
  SerialUSB.println();

  SystemReset();
}

void SystemReset()
{
  for (;;)
  {
    SerialUSB.println("RESET!!!");
    delay(SYSTEM_RESET_DELAY);
    Wio.SystemReset();
  }
}

////////////////////////////////////////////////////////////////////////////////
