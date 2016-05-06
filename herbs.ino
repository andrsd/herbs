/*
  herbs.ino
  by David Andrs, 2016
*/

#include <SPI.h>
#include <WiFi.h>
#include "DHT/DHT.h"

//
// DHT 11
//

// digital pin the DHT11 is connected to
#define DHT_PIN         15
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
bool dry_notified[N_HERBS];

// last time you connected to the server, in milliseconds
unsigned long last_connection_time = 0;
// delay between updates, in milliseconds
const unsigned long posting_interval = 10L * 1000L;
// reset notifications after this amount of measurements
const unsigned int dry_notify_reset_counter = 12;
// number of samples collected so far
unsigned int n_samples;

char str[512];
char s_number[32];

// Initialize the Wifi client library
WiFiClient client;
#define USER_AGENT_HEADER     "User-Agent: ArduinoWiFi/1.1"


/// Reset the notifications
void reset_dry_notified() {
  for (unsigned int i = 0; i < N_HERBS; i++)
    dry_notified[i] = false;
}

void setup() {
  Serial.begin(9600);
  // wait for serial port to connect. Needed for native USB port only
  while (!Serial);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    while (true);
  }

  dht.begin();

  n_samples = 0;
  reset_dry_notified();
}

void connect_to_wifi() {
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  int attempts = 10;
  while ((WiFi.status() != WL_CONNECTED) && (attempts > 0)) {
    delay(1000);
    attempts--;
  }
}

void read_values() {
  Serial.println("Reading values...");

  // read moisture levels
  for (int i = 0; i < N_HERBS; i++) {
    int val = analogRead(analog_pin[i]);
    moisture[i] = val / 1023.0;
    Serial.print(herb_name[i]);
    Serial.print(": ");
    Serial.print(moisture[i]);
    Serial.println(" %");
  }

  // Temperature
  temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println("Failed to read temperature from DHT sensor!");
    temperature = 0;
  }
  else {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" C");
  }

  // Humidity
  humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println("Failed to read humidity from DHT sensor!");
    humidity = 0;
  }
  else {
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  }
}

void upload_values() {
  Serial.print("Connecting to ");
  Serial.print(CLOUD_HOSTNAME);
  Serial.print("... ");
  if (client.connect(CLOUD_HOSTNAME, CLOUD_PORT)) {
    Serial.println("done.");

    strcpy(str, "'{ ");
    for (int i = 0; i < N_HERBS; i++) {
      snprintf(s_number, sizeof(s_number) - 1, "\"%s\": %.2f, ", herb_name[i], moisture[i]);
      strcat(str, s_number);
    }
    snprintf(s_number, sizeof(s_number) - 1, "\"temperature\": %.2f, ", temperature);
    strcat(str, s_number);
    snprintf(s_number, sizeof(s_number) - 1, "\"humidity\": %.2f", humidity);
    strcat(str, s_number);
    strcat(str, " }'");
    int len = strlen(str);

    client.print("POST ");
    client.print(CLOUD_URI);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(CLOUD_HOSTNAME);
    client.println(USER_AGENT_HEADER);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(len);
    client.println("Connection: close");
    client.println();
    client.println(str);

    // read server response
    while (client.available())
      char c = client.read();
    // just in case
    client.stop();
  }
  else {
    Serial.println("failed.");
  }
}

void notify_if_dry() {
  int n_dry = 0;
  for (int i = 0; i < N_HERBS; i++)
    if (moisture[i] < DRY_THRESHOLD) {
      dry_idx[n_dry] = i;
      n_dry++;
    }
  // all herbs are watered
  if (n_dry == 0)
    return;

  Serial.print("Connecting to maker.ifttt.com... ");
  if (client.connect("maker.ifttt.com", 80)) {
    Serial.println("done.");

    strcpy(str, "'{ \"value1\" : \"");
    if (n_dry == 1) {
      strcat(str, herb_name[dry_idx[0]]);
      strcat(str, " needs water.");
      dry_notified[dry_idx[0]] = true;
    }
    else {
      for (int i = 0; i < n_dry; i++) {
        strcat(str, herb_name[dry_idx[i]]);
        if (i < n_dry - 2)
          strcat(str, ", ");
        else if (i < n_dry - 1)
          strcat(str, " and ");
        dry_notified[dry_idx[i]] = true;
      }
      strcat(str, " need water.");
    }
    strcat(str, "\" }'");
    int len = strlen(str);

    client.print("POST /trigger/herbs/with/key/");
    client.print(IFTTT_MAKER_KEY);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(CLOUD_HOSTNAME);
    client.println(USER_AGENT_HEADER);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(len);
    client.println("Connection: close");
    client.println();
    client.println(str);

    // read server response
    while (client.available())
      char c = client.read();
    // disconnect
    client.stop();
  }
  else {
    Serial.println("failed.");
  }
}

void loop() {
  if (millis() - last_connection_time > posting_interval) {
    Serial.print("Connecting to ");
    Serial.print(WLAN_SSID);
    Serial.print("... ");
    connect_to_wifi();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("connected (");
      Serial.print(WiFi.localIP());
      Serial.println(")");

      read_values();
      upload_values();
      n_samples++;

      if (n_samples >= dry_notify_reset_counter) {
        reset_dry_notified();
        n_samples = 0;
      }
      notify_if_dry();

      WiFi.disconnect();
      last_connection_time = millis();
    }
    else {
      Serial.println("failed");
    }
  }
}
