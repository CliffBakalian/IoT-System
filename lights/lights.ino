#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#define MAX_LR_MIC_SAMPLES 2048
#define LOCALIZATION_FREQ 300

// global variables

//internetless router info 
const char* ssid = "The Nexus";
const char* password = "iamconnected";
String server_addr = "http://192.168.0.5:42";
ESP8266WebServer server(80);
int ID = -1;

// main stuff

void setup() {
  delay(1000);
  internet_setup();
}

void loop() {
  server.handleClient();
}

// utility functions

void beep(int ms, int hz) {
  // CAREFUL, tone will interfere with PWM output on pins 3 and 11
  // in our schematic, should be ok?
  tone(speakerPin, hz, ms);
}

float mcs_to_cm(long microseconds) {
  return microseconds / 29.0 / 2.0;
}

// internet responders

void do_post(String endpoint, String message) {
  HTTPClient http;
  if (http.begin(server_addr+endpoint)){
    int httpCode = http.POST(message);
    http.end();
  }
}

void ping_server() {
  HTTPClient http;
  if (http.begin(server_addr+"/reg")) {
    my_time = millis();
    String message = "{\"clock\":";
    message += my_time;
    message += ",\"ip\":\"";
    message += WiFi.localIP().toString();
    message += "\"}";
    int httpCode = http.POST(message);
    // server never returns bad http code though so...
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) { // httpCode == HTTP_CODE_MOVED_PERMANENTLY
        String payload = http.getString();
        ID = payload.toInt();
      } else {
        beep(100, 59);
      }
    } else {
      beep(100, 69);
    }
    http.end();
  }
}

void send_loc(String message) {
  do_post("/loc", message);
}

void send_mov(String message) {
  do_post("/mov", message);
}

void send_debug(String message) {
  do_post("/debug", message);
}

// setup functions

void ready_melody() {
  beep(125, 293);
  delay(125);
  beep(125, 329);
  delay(125);
  beep(125, 370);
  delay(125);
  beep(125, 440);
}

void sound_setup(){
  pinMode(speakerPin, OUTPUT);
  //
  beep(100, 110);
  delay(150);
  beep(50, 110);
  delay(100);
  beep(50, 110);
  delay(500);
}

void usonic_setup(){
  pinMode(echoPin, INPUT);
  pinMode(trigPin, OUTPUT);
  //
  beep(100, 220);
  delay(500);
}

boolean sync_board(int max_ms) {
  unsigned long start_time = millis();
  char buf[1];
  short count = 0;
  while (millis() - start_time < max_ms && count < 8) {
    Serial.write("ssssssss");
    while (Serial.available()) {
      Serial.readBytes(buf, 1);
      if (buf[0] == 's') {
        count += 1;
      }
    }
    delay(50);
  }
  while (Serial.available()) {
    Serial.read();
    delay(1);
  } // clear buffer
  return count >= 4;
}

void internet_setup(){
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    beep(100, 49);
    delay(1000);
  }
  beep(250, 270);
  while(ID < 0){
    ping_server();  
    delay(1000);
  }
  server.on("/", handle_root); // need?
  server.on("/loc", get_loc_data);
  server.on("/mov", get_mov_data);
  server.on("/ult", get_ult_data);
  server.on("/bep", get_bep_data);
  server.onNotFound(handle_not_found); // need?
  server.begin();
  beep(100, 300);
  delay(200);
  beep(100, 300);
  delay(500);
//  send_debug(String(sizeof(byte))); // 1
//  send_debug(String(sizeof(short))); // 2
//  send_debug(String(sizeof(int))); // 4 (on esp)
//  send_debug(String(sizeof(long))); // 4
//  send_debug(String(sizeof(unsigned short)));
//  send_debug(String(sizeof(unsigned int)));
//  send_debug(String(sizeof(unsigned long)));
}

// sensor interaction functions

float read_ult(int samples) {
  float tot = 0;
  for (int i = 0; i < samples; i++) {
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    // Reads the echoPin, returns the sound wave travel time in microseconds
    tot += pulseIn(echoPin, HIGH);
  }
  return mcs_to_cm(tot) / samples;
}

// void test_usonic() {
//   Serial.print(read_ult(3));
//   Serial.print("cm");
//   Serial.println();
// }

// internet handlers

void handle_root() {
  server.send(200, "text/plain", "hello from esp8266!\n");
}

void handle_not_found(){
  server.send(404, "text/plain", ":(\n");
}

void get_loc_data(){
//  String message = ""; message += millis();
//  server.send(200, "text/plain", message);
  String body = server.arg("plain");
  int comma = body.indexOf(',');
  unsigned short post_time = (unsigned short) body.substring(comma+1,body.indexOf(',', comma+1)).toInt();
  unsigned short post_delay = (unsigned short) body.substring(body.indexOf(',', comma+1)+1).toInt();
  if (body.charAt(0) == 'l') {
    listen_sig(post_time, post_delay);
  } else if (body.charAt(0) == 's') {
    speaker_sig(post_time, post_delay);
  } else {
    server.send(200, "text/plain", "invalid command\n");
  }
}

void get_mov_data(){
  String body = server.arg("plain");
  unsigned short param = (unsigned short) body.substring(body.indexOf(',')+1).toInt();
  char sig = body.charAt(0);
  if (sig == 'f' || sig == 'b' || sig == 'r') {
    server.send(200, "text/plain", "Hello there! General Kenobi.\n");
    motor_sig(sig, param);
  } else {
    server.send(200, "text/plain", "invalid command\n");
  }
}

void get_ult_data() {
  int samples = server.arg("plain").toInt();
  server.send(200, "text/plain", String(read_ult(samples)));
}

void get_bep_data() {
  // just do a beep routine
  int hz = server.arg("plain").toInt();
  beep(100, hz);
  server.send(200, "text/plain", "bepis\n");
}

// controls / sensor interfaces

// ESP STORES DATA IN REVERSE BYTE ORDER
// SO DOES ARDUINO? (assume yes)
// this is "little endian" ness

void listen_sig(unsigned short post_time, unsigned short delay_time) {
  unsigned long setup_start_time = millis();
  byte buf[5] = {0};
  int ptr = 0;
  boolean flag = false, flop = false;
  unsigned long total_time;
  for (int i = 0; i < 2*2*MAX_LR_MIC_SAMPLES; i++) {
    samples[i] = 0;
  }
  buf[0] = 'L';
  memcpy(buf+1, (byte *) &post_time,  sizeof(short));
  memcpy(buf+3, (byte *) &delay_time, sizeof(short));
  Serial.write(buf, 5);
  Serial.flush();
  ((unsigned long *) buf)[0] = 0x00000000;
  while ((buf[0] ^ 0xf7) && (buf[1] ^ 0xf7)) {
    buf[1] = buf[0];
    buf[0] = Serial.read();
    delay(1);
  }
  server.send(200, "text/plain", String(setup_start_time)+","+String(millis())); // critical for timing
//  delay(delay_time);
  unsigned long true_start = 0;
  while (ptr < 2*2*MAX_LR_MIC_SAMPLES && !flag) {
    while (Serial.available() > 0) {
      samples[ptr] = Serial.read();
      if (flop) {
        flag = ptr > 3 && !(
          (samples[ptr-3] ^ 0xff) ||
          (samples[ptr-2] ^ 0xff) ||
          (samples[ptr-1] ^ 0xff) ||
          (samples[ptr]   ^ 0xff));
        if (flag) break;
        ptr++;
        if (ptr >= 2*2*MAX_LR_MIC_SAMPLES) break;
      } else {
        flop = (samples[ptr] == 0xfe);
        if (flop) true_start = millis();
      }
//      delay(1); // we can't have this here if baud is too high
    }
//    delay(1); // we can't have this here if baud is too high
  }
  Serial.readBytes((byte *) &total_time, sizeof(long));
  String message = "{\"start\":";
  message += true_start;
  message += ",\"total\":";
  message += total_time;
  message += ",\"id\":";
  message += ID;
  message += ",\"data\":\"";
  for (int i = 0; i < ptr-4; i += 2) {
    message += String(((unsigned short *)(samples+i))[0], HEX);
    message += ",";
    delay(1);
  }
  message.setCharAt(message.length()-1, '\"'); // replace comma
  message += "}";
  send_loc(message);
}

void speaker_sig(unsigned short post_time, unsigned short delay_time) {
  server.send(200, "text/plain", String(millis())); // critical for timing
  delay(delay_time);
  unsigned long true_start = millis();
  beep(post_time, LOCALIZATION_FREQ);
  delay(post_time);
  String message = "{\"id\":";
  message += ID;
  message += ",\"start\":";
  message += true_start;
  message += "}";
  send_loc(message);
}

void motor_sig(char sig, short param) {
  // if rotate,  param == degrees
  // if fwd/bak, param == travel distance
  byte out_buf[5];
  if (sig == 'r') {
    memcpy(out_buf, &sig, 1);
    memcpy(out_buf+1, &param, sizeof(short));
    memcpy(out_buf+3, &param, sizeof(short));
    while (Serial.availableForWrite() < 5) {;}
    Serial.write(out_buf, 5);
    Serial.flush();
    // wait for finish signal
    ((unsigned long *) out_buf)[0] = 0x00000000;
//    send_debug(String(((unsigned long *) out_buf)[0]));
    while ((out_buf[0] ^ 0xf7) || (out_buf[1] ^ 0xf7)) {
      if (Serial.available()) {
        out_buf[1] = out_buf[0];
        out_buf[0] = Serial.read();
//        send_debug(String(out_buf[0], HEX)+" "+String(out_buf[1], HEX));
      }
      delay(1);
    }
//    send_debug("hi there");
    float ang = 0;
    Serial.readBytes((byte *) &ang, sizeof(float));
//    send_debug(String( ((long *) &ang)[0], HEX ));
//    delay(250);
    // tell server what happened
    String message = "{\"id\":";
    message += ID;
    message += ",\"rot\":";
    message += ang;
    message += ",\"mov\":\"r\"}";
    send_mov(message);
  } else {
    if (sig == 'b') {
      param *= -1;
    }
    boolean flag = true;
    unsigned short max_computation = 20000;
    out_buf[0] = 'f';
    memcpy(out_buf+1, &max_computation, sizeof(short));
    memcpy(out_buf+3, &max_computation, sizeof(short));
    while (Serial.availableForWrite() < 5) {;}
    Serial.write(out_buf, 5);
    Serial.flush();
    // todo, control code for rotation on nano?
    // int param == centimeters of wanted fwd/bak travel distance
    unsigned long start_time = millis();
    unsigned short max_motor_time = 1;
    int pause_count = 0;
    // todo -- ensure start_ult_dist is within 2 and 500 or whatever bounds
    float start_ult_dist = read_ult(10), curr_ult_dist, delta_distance = 0, c;
    // while within computation time
    // and have not traveled enough distance (fwd / bak)
    while (millis() - start_time < max_computation && flag && sig != 'x') {
      // compute distance to-go
      curr_ult_dist = read_ult(5);
      delta_distance = start_ult_dist - curr_ult_dist;
      // decide which motor action to take
      c = param - delta_distance;
      if (sig == 'p') {
        pause_count++;
      } else {
        pause_count = 0;
      }
      if (curr_ult_dist < 10) {
        sig = 'b'; // we're too close to a forward obstacle
      } else if (c > 0.2) {
        sig = 'f';
      } else if (c < -0.2) {
        sig = 'b';
      } else if (pause_count > 8) {
        sig = 'x'; // stop!
      } else {
        sig = 'p';
      }
      // decide how long to keep motors on (max)
      max_motor_time = 50; // todo -- function of acceleration and distance-to-go
      // tell nano what to do
      memcpy(out_buf,   &sig, 1);
      memcpy(out_buf+1, &max_motor_time, sizeof(short));
      memcpy(out_buf+3, &max_motor_time, sizeof(short));
      //while (Serial.availableForWrite() < 5) {;}
      Serial.write(out_buf, 5);
      Serial.flush(); // wait until buffer is fully transmitted
      // repeat
      if (param < 0) { // account for fwd/bwd
        flag = delta_distance > param;
      } else {
        flag = delta_distance < param;
      }
      delay(2*max_motor_time);
    }
    if (sig != 'x') {
      // stop motors
      sig = 'x';
      max_motor_time = 0;
      memcpy(out_buf,   &sig, 1);
      memcpy(out_buf+1, &max_motor_time, sizeof(short));
      memcpy(out_buf+3, &max_motor_time, sizeof(short));
      Serial.write(out_buf, 5);
      Serial.flush();
    }
    ((unsigned long *) out_buf)[0] = 0xffffffff;
    // this is not guaranteed to wait for the NANO?
    // TODO!
    while (Serial.available() > 0) {
      out_buf[0] = out_buf[0] & Serial.read();
      if (out_buf[0] | out_buf[1] == 0) break;
      out_buf[1] = out_buf[0];
      delay(1);
    }
//    delay(250);
    // tell server what happened
    String message = "{\"id\":";
    message += ID;
    message += ",\"start\":";
    message += start_ult_dist;
    message += ",\"end\":";
    message += read_ult(10);
    message += ",\"mov\":\"m\"}";
    send_mov(message);
  }
}
