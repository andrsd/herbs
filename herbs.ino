/*
  herbs.ino
  by David Andrs, 2016

  MIT license
*/

#include <SPI.h>
#include <WiFi101.h>
#include "DHT/DHT.h"

//
// DHT 11
//

// digital pin the DHT11 is connected to
#define DHT_PIN         6
#define DHT_TYPE        DHT11

DHT dht(DHT_PIN, DHT_TYPE);

#include "config.h"

#define DRY_THRESHOLD   0.5
#define N_HERBS         6

const char * herb_name[N_HERBS] = {
  "sage",
  "thyme",
  "chives",
  "parsley",
  "rosemary",
  "basil"
};

int analog_pin[N_HERBS] = {
  A0,
  A1,
  A2,
  A3,
  A4,
  A5
};

float moisture[N_HERBS];
float temperature = 0;
float humidity = 0;

// index of the herb that is dry
int dry_idx[N_HERBS];

// last time we read data, in milliseconds
unsigned long last_read_time = 0;
// delay between updates, in milliseconds
const unsigned long read_interval = 1800L * 1000L;

char str[512];
char s_number[32];

// Initialize the Wifi client library
WiFiClient client;
#define USER_AGENT_HEADER     "User-Agent: ArduinoWiFi/1.1"

int herbs_status = 0;
int notification_status[N_HERBS];

void setup() {
  Serial.begin(9600);
  // wait for serial port to connect. Needed for native USB port only
  //while (!Serial);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    while (true);
  }

  dht.begin();

  Serial.println("herb monitor started");

  last_read_time = millis();
  for (unsigned int i = 0; i < N_HERBS; i++)
    notification_status[i] = 0;

  connect_to_wifi();
}

void connect_to_wifi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.print(WLAN_SSID);
    Serial.print("... ");

    WiFi.begin(WLAN_SSID, WLAN_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      delay(3000);
    }

    Serial.println("done");
  }
}

void read_values() {
  Serial.println("Reading values...");

  // read moisture levels
  for (int i = 0; i < N_HERBS; i++) {
    int val = analogRead(analog_pin[i]);
    moisture[i] = val / 1023.0;
    Serial.print(" - ");
    Serial.print(herb_name[i]);
    Serial.print(": ");
    Serial.print(moisture[i]);
    Serial.println(" %");
  }

  // Temperature
  temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println(" - failed to read temperature from DHT sensor!");
    temperature = 0;
  }
  else {
    Serial.print(" - temperature: ");
    Serial.print(temperature);
    Serial.println(" C");
  }

  // Humidity
  humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println(" - failed to read humidity from DHT sensor!");
    humidity = 0;
  }
  else {
    Serial.print(" - humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  }
}

void upload_values() {
  Serial.print("Uploading to ");
  Serial.print(CLOUD_HOSTNAME);
  Serial.print("... ");

  bool success = false;
  int attempts = 3;
  while (!success && attempts > 0) {
    client.stop();
    if (client.connect(CLOUD_HOSTNAME, CLOUD_PORT)) {
      strcpy(str, "{ ");
      for (int i = 0; i < N_HERBS; i++) {
        snprintf(s_number, sizeof(s_number) - 1, "\"%s\": %.2f, ", herb_name[i], moisture[i]);
        strcat(str, s_number);
      }
      snprintf(s_number, sizeof(s_number) - 1, "\"temperature\": %.2f, ", temperature);
      strcat(str, s_number);
      snprintf(s_number, sizeof(s_number) - 1, "\"humidity\": %.2f", humidity);
      strcat(str, s_number);
      strcat(str, " }");
      int len = strlen(str);

      client.print("POST ");
      client.print(CLOUD_URI);
      client.println(" HTTP/1.1");
      client.print("Host: ");
      client.println(CLOUD_HOSTNAME);
      client.println(USER_AGENT_HEADER);
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.print("Content-Length: ");
      client.println(len);
      client.println();
      client.println(str);

      success = true;
    }

    attempts--;
  }

  if (success)
    Serial.println("done");
  else
    Serial.println("failed");
}

void notify_if_dry() {
  int n_dry = 0;
  for (int i = 0; i < N_HERBS; i++)
    if (moisture[i] < DRY_THRESHOLD) {
      // we found a dry herb
      if (notification_status[i] == 0) {
        // which has not been reported yet
        dry_idx[n_dry] = i;
        n_dry++;
      }
    }
    else {
      // enough moisture, reset notification status
      notification_status[i] = 0;
    }
  // all herbs are watered
  if (n_dry == 0)
    return;

  Serial.print("Connecting to maker.ifttt.com... ");
  client.stop();
  if (client.connect("maker.ifttt.com", 80)) {
    Serial.println("done");

    strcpy(str, "{ \"value1\" : \"");
    if (n_dry == 1) {
      strcat(str, herb_name[dry_idx[0]]);
      strcat(str, " needs water.");
    }
    else {
      for (int i = 0; i < n_dry; i++) {
        strcat(str, herb_name[dry_idx[i]]);
        if (i < n_dry - 2)
          strcat(str, ", ");
        else if (i < n_dry - 1)
          strcat(str, " and ");
      }
      strcat(str, " need water.");
    }
    strcat(str, "\" }");
    int len = strlen(str);

    client.print("POST /trigger/herbs/with/key/");
    client.print(IFTTT_MAKER_KEY);
    client.println(" HTTP/1.1");
    client.println("Host: maker.ifttt.com");
    client.println(USER_AGENT_HEADER);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.print("Content-Length: ");
    client.println(len);
    client.println();
    client.println(str);

    Serial.println("Notification sent");

    // mark herbs that were reported
    for (int i = 0; i < n_dry; i++)
      notification_status[dry_idx[i]] = 1;
  }
  else {
    Serial.println("failed");
  }
}

void loop() {
  if (millis() - last_read_time > read_interval) {
    read_values();
    herbs_status = 1;
    last_read_time = millis();
  }

  if (herbs_status == 1) {
    connect_to_wifi();
    upload_values();
    notify_if_dry();
    herbs_status = 2;
  }
}
