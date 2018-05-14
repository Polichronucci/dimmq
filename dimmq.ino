/*
  Copyright (C) 2018  Polichronucci

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
Still not implemented:
  what happens if wifi hangs

If a delay is holding up the processor.
The MQTT is also hold up.
The push button works though because of the interrupt.
The timer1 as well, for the same reason.

  GPIO05/D01 GATE
  GPIO04/D02 SYNC
  
  GPIO14/D05 CLK
  GPIO12/D06 DT

  GPIO13/D07 SW

*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Encoder.h>

// WIFI
WiFiClient espDimmer;
const char* ssid = "YOURWIFI-SSID";
const char* password = "YOURWIFI-PASS";
const char* mqtt_server = "MQTTSERVER";
const int mqtt_port = 1883;
const char* client_name = "espDimmer1";

// MQTT
PubSubClient client(espDimmer);
char mqtt_state[50], mqtt_command[50], mqtt_brightness_state[50];
String mqtt_payload_off = ("OFF");
long lastMsg = 0;
char msg[50];
int value = 0;

// KNOB
int BTN_pin=13;
Encoder knob(14, 12);
int oldPosition=-999;

// DIMMER
int AC_pin=5;			// Output to Opto Triac
int ZX_pin=4;
volatile int counter=127;	// Variable to use as a counter volatile as it is in an interrupt
// Boolean to store a "switch" to tell us if we have crossed zero
volatile boolean zero_cross=0, current_dim=0, previous_dim=0;
int target_dim=0;		// Dimming level (0-128)  0 = on, 128 = 0ff

// Delay
long MillisKNOB = 0, MillisDIM = 0, MillisMQTT = -50000;

// Interrupt timer
void ICACHE_RAM_ATTR dim_check() {

  timer1_write(375);

  int long currentMillis = millis();

  if (currentMillis - MillisDIM > 10) {
    if (current_dim < target_dim)
      current_dim++;
    else if (current_dim > target_dim)
      current_dim--;
    MillisDIM = millis();
  }

  if(zero_cross == true) {              
    if(counter <= current_dim) {                     
      digitalWrite(AC_pin, HIGH); // turn on light
      counter=127;  // reset time step counter
      zero_cross = false; //reset zero cross detection
    } 
    else
      counter--; // increment time step counter    
  }
}  

void setup() {
  Serial.begin(115200);

// KNOB on GPIO 14, need pullup. It triggers interrupt handleKey
  pinMode(BTN_pin, INPUT_PULLUP);
  attachInterrupt(BTN_pin, handleKey, RISING);

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  snprintf(mqtt_command, 50, "office/%s", client_name);
  snprintf(mqtt_state, 50, "office/%s/status", client_name);
  snprintf(mqtt_brightness_state, 50, "office/%s/brightness", client_name);

  pinMode(AC_pin, OUTPUT);                          // Set the Triac pin as output
  attachInterrupt(ZX_pin, zero_cross_detect, RISING);    // Attach an Interrupt to Pin 2 (interrupt 0) for Zero Cross Detection

  timer1_disable();
  timer1_isr_init();
  timer1_attachInterrupt(dim_check);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  timer1_write(375);
}

void zero_cross_detect() {    
  zero_cross = true;               // set the boolean to true to tell our dimming function that a zero cross has occurred
  counter=127;
  digitalWrite(AC_pin, LOW);       // turn off TRIAC (and AC)
}                                                                  

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

//  while (WiFi.status() != WL_CONNECTED) {
//    delay(500);
//    Serial.print(".");
//  }
//  Serial.println("");
//  Serial.println("WiFi connected");
//  Serial.println("IP address: ");
//  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  payload[length] = '\0';            		// terminate string with '0
  String payload_str = String((char*)payload);  // convert to string
  Serial.println(payload_str);

  byte payload_b[length];
  for (int i = 0; i < length; i++) {
    payload_b[i] = payload[i];
  }

  int payload_int = payload_str.toInt();

  if (payload_str == mqtt_payload_off) {
    target_dim = 0;
    client.publish(mqtt_state, "OFF", true);
  }
  else if (payload_int > 0 ) {
    client.publish(mqtt_brightness_state, payload_b, (char)length, true);
    client.publish(mqtt_state, "ON", true);

    target_dim = payload_int;
  }
}

void reconnect() {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(client_name)) {
      Serial.println("connected");
      client.subscribe(mqtt_command);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 30 seconds");
    }
  MillisMQTT = millis();
}

void handleKey() {
  if (target_dim != 0) {
    previous_dim = target_dim;
    target_dim = 0;
    client.publish(mqtt_brightness_state, "0", true);
    client.publish(mqtt_state, "OFF", true);
  }
  else {
    target_dim = previous_dim;

    char char_dim[3];
    sprintf (char_dim, "%i", target_dim);

    client.publish(mqtt_brightness_state, char_dim, true);
    client.publish(mqtt_state, "ON", true);
  }
}

void loop() {
  int long currentMillis = millis();

//  if (WiFi.status() != WL_CONNECTED)
//    setup_wifi();

  if (!client.connected() && currentMillis - MillisMQTT > 30000)
    reconnect();

  if (client.connected())
  client.loop();

  int newPosition;

  if (currentMillis - MillisKNOB > 500) {
    MillisKNOB = millis();
    newPosition = knob.read();

    if (newPosition != oldPosition) {
      target_dim += newPosition - oldPosition;

      if (target_dim < 0)
        target_dim = 0;
      else if (target_dim > 127)
        target_dim = 127;

      if (oldPosition == 0)
        client.publish(mqtt_state, "ON", true);

      char char_dim[3];
      sprintf (char_dim, "%i", target_dim);
      client.publish(mqtt_brightness_state, char_dim, true);

      oldPosition = newPosition;
    }
  }
}
