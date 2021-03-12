#include "pitches.h"
#define LIGHT_SIZE 5
#define OFFSET 8
byte commandByte;
byte noteByte;
byte velocityByte;
int speakerPin = 2;
int middle_c = 262;
int lights[LIGHT_SIZE];
int notes[25] = {
  NOTE_C3, NOTE_CS3, NOTE_D3, NOTE_DS3, NOTE_E3, NOTE_F3, NOTE_FS3, NOTE_G3, NOTE_GS3,
  NOTE_A3, NOTE_AS3, NOTE_B3, NOTE_C4, NOTE_CS4, NOTE_D4, NOTE_DS4, NOTE_E4, NOTE_F4, 
  NOTE_FS4, NOTE_G4, NOTE_GS4, NOTE_A4, NOTE_AS4, NOTE_B4, NOTE_C5
};
void setup(){
  Serial.begin(31250);
  pinMode(speakerPin, OUTPUT);
  for(int i =0; i< LIGHT_SIZE; i++){
    lights[i] = i;
    pinMode(OFFSET + i, OUTPUT);
    digitalWrite(OFFSET + i, HIGH);}}
void light(byte note,byte v){
  int val = note - 47;
  if(v != 0)
    tone(speakerPin,notes[val]);
  else
    noTone(speakerPin);
  for(int i =0; i< LIGHT_SIZE; i++){
    if(val>>i & 1 && v != 0)
      digitalWrite(OFFSET + i,HIGH);
    else
      digitalWrite(OFFSET + i,LOW);}}
void checkMIDI(){
  do{
    if (Serial.available()){
      commandByte = Serial.read();//read first byte
      noteByte = Serial.read();//read next byte
      velocityByte = Serial.read();//read final byte
      if ((noteByte >= 48 && noteByte <= 72))// && velocityByte > 0)
        light(noteByte, velocityByte);}}
  while (Serial.available() > 2);//when at least three bytes available
  }
void loop(){
  checkMIDI();
  delay(100);
}
