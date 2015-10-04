#include <Usb.h>
#include <usbh_midi.h>

#define LED_NUMBER 12
#define CMD 0
#define DATA1 1
#define DATA2 2
#define NOTE DATA1
#define VELOCITY DATA2
#define NOTE_ON 0x90
#define NOTE_OFF 0x80
#define CTRL 0xF0
#define CMD_MASK 0xf0
#define CMD_MASKED (msg[CMD] & CMD_MASK)
#define RESET 36

int shiftPin = 5; // SH_CP
int storagePin = 6; // ST_CP
int dataPin = 7; // DS
int notesPressed[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; //c,d,e,f,g,a,b,cis,dis,fis,gis,ais

USB  Usb;
MIDI  Midi(&Usb);
boolean first = true;
int lastShift;

void setup() {
  pinMode(shiftPin, OUTPUT);
  pinMode(storagePin, OUTPUT);
  pinMode(dataPin, OUTPUT);
  allLEDs(HIGH);
  if (Usb.Init() == -1) // USB initialisieren
    while(1); // bei Fehlschlag "ausschalten"
  delay(200); // Zeit zur Initialisierung von USB
}

void loop() {
  Usb.Task();
  if (Usb.getUsbTaskState() == USB_STATE_RUNNING) { // wenn das USB-Gerät läuft
    if (first) {
      allLEDs(LOW);
      first = false;
    }
    readNote(); // frage das MIDI-Gerät auf NoteOn/Off-Messages ab
  }
  //delay(1);
  delayMicroseconds(10);
}

void readNote() {
  byte msg[3];
  if (Midi.RcvData(msg) == true) { // empfange MIDI-Nachricht
    if (CMD_MASKED == NOTE_OFF ||
      (CMD_MASKED == NOTE_ON && msg[VELOCITY] == 0)) { // wenn Taste losgelassen
      noteOff(msg[NOTE]);
    }
    else if (CMD_MASKED == NOTE_ON) { // wenn Taste gedrückt
      noteOn(msg[NOTE], msg[VELOCITY]);
    }
  }
}

void noteOn(byte note, byte velocity) {
  if (note == RESET) {
    allLEDs(LOW);
    for (int i = 0; i < LED_NUMBER; i++)
      notesPressed[i] = 0;
  }
  byte noteValue = getNoteValue(note);
  ledOn(noteValue);
  notesPressed[noteValue]++;
}

void noteOff(byte note) {
  byte noteValue = getNoteValue(note);
  notesPressed[noteValue]--;
  if (notesPressed[noteValue] < 1)
    ledOff(noteValue);
}

byte getNoteValue(byte note) { // gibt den Notenwert (C-H) einer Note zurück
  if (note % 12 == 0) return 0; //c
  if (note % 12 == 2) return 1; //d
  if (note % 12 == 4) return 2; //e
  if (note % 12 == 5) return 3; //f
  if (note % 12 == 7) return 4; //g
  if (note % 12 == 9) return 5; //a
  if (note % 12 == 11) return 6; //b
  if (note % 12 == 1) return 7; //cis
  if (note % 12 == 3) return 8; //dis
  if (note % 12 == 6) return 9; //fis
  if (note % 12 == 8) return 10; //gis
  if (note % 12 == 10) return 11; //ais
}

void allLEDs(boolean signal) { // schaltet alle LEDs an oder aus
  signal ? sendWord(0b1111111111111111) : sendWord(0);
}

void ledOn(int note) {
  if (note == 0) //c
    sendWord(lastShift | 0b1);
  if (note == 1) //d
    sendWord(lastShift | 0b10);
  if (note == 2) //e
    sendWord(lastShift | 0b100);
  if (note == 3) //f
    sendWord(lastShift | 0b1000);
  if (note == 4) //g
    sendWord(lastShift | 0b10000);
  if (note == 5) //a
    sendWord(lastShift | 0b100000);
  if (note == 6) //b
    sendWord(lastShift | 0b1000000);
  if (note == 7) //cis
    sendWord(lastShift | 0b100000000);
  if (note == 8) //dis
    sendWord(lastShift | 0b1000000000);
  if (note == 9) //fis
    sendWord(lastShift | 0b10000000000);
  if (note == 10) //gis
    sendWord(lastShift | 0b100000000000);
  if (note == 11) //ais
    sendWord(lastShift | 0b1000000000000);
}

void ledOff(int note) {
  if (note == 0) //c
    sendWord(lastShift & 0b1111111111111110);
  if (note == 1) //d
    sendWord(lastShift & 0b1111111111111101);
  if (note == 2) //e
    sendWord(lastShift & 0b1111111111111011);
  if (note == 3) //f
    sendWord(lastShift & 0b1111111111110111);
  if (note == 4) //g
    sendWord(lastShift & 0b1111111111101111);
  if (note == 5) //a
    sendWord(lastShift & 0b1111111111011111);
  if (note == 6) //b
    sendWord(lastShift & 0b1111111110111111);
  if (note == 7) //cis
    sendWord(lastShift & 0b1111111011111111);
  if (note == 8) //dis
    sendWord(lastShift & 0b1111110111111111);
  if (note == 9) //fis
    sendWord(lastShift & 0b1111101111111111);
  if (note == 10) //gis
    sendWord(lastShift & 0b1111011111111111);
  if (note == 11) //ais
    sendWord(lastShift & 0b1110111111111111);
}

void sendWord(int value){
  lastShift = value;
  digitalWrite(storagePin, LOW);
  shiftOut(dataPin, shiftPin, MSBFIRST, value >> 8);
  shiftOut(dataPin, shiftPin, MSBFIRST, value & 255);
  digitalWrite(storagePin, HIGH);
}

