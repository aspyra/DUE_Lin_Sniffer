#include "Arduino.h"
#define RX 19
#define IGNORE_DIAG 52
#define BAUD_RATE 9600
#define MEM_SIZE 64
enum mode{waiting_for_break = 0, measuring_break, reading_bytes};
volatile mode LIN_mode;
volatile unsigned long break_time;

//struktura do przechowywania ramek
struct data_frame{
  uint8_t pid = 0x00;
  uint8_t data_count = 0;
  uint8_t data[8] = {0};
  uint8_t chk = 0x00;
};

//zmienne globalne
data_frame FRAME_MEMORY[MEM_SIZE];
uint8_t frame_loop[MEM_SIZE];
uint8_t frame_loop_count = 0;
uint8_t saved_frames_count = 0;

//przerwanie na pinie RX
void rx_interrupt(){
  if(LIN_mode == measuring_break && digitalRead(RX)){
    if(micros() - break_time > 11000000UL/BAUD_RATE){
      LIN_mode = reading_bytes;
      return;
    }
    else{
      LIN_mode = waiting_for_break;
      return;
    }
  }

  if(LIN_mode == waiting_for_break && !digitalRead(RX)){
    break_time = micros();
    LIN_mode = measuring_break;
    return;
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(RX, INPUT_PULLUP);
  pinMode(IGNORE_DIAG, INPUT_PULLUP);
}

String HexToString(const uint8_t byte){
  String temp = "";
  if(byte <= 0x0F)
    temp += "0";
  temp += String(byte, HEX);
  return temp;
}

void printFrame(data_frame frame, bool if_chk = true){
      String out = "\n" + HexToString(frame.pid) + " | ";
    for(int i = 0; i < frame.data_count; ++i)
      out += String(frame.data[i], HEX) + " ";
    if(if_chk)
      out += "(" + String(frame.chk, HEX) + ")";
    Serial.println(out);
}

void dataToFrame(data_frame &frame, uint8_t * data, uint8_t data_count){
    frame.pid = data[1] & 0x3F;
    if(data_count >= 3)
      frame.chk = data[data_count - 1];
    if(data_count >= 4){
      frame.data_count = data_count - 3;
      memcpy(frame.data, data+2, frame.data_count);
    }
}

void loop(){
  //na początku włączamy interrupt
  LIN_mode = waiting_for_break;
  attachInterrupt(19, rx_interrupt, CHANGE);
  while(LIN_mode != reading_bytes); //czekamy aż interrupt da znać, że można zacząć czytać dane
  //wyłączyamy szybko interrupt i zaczynamy odczytywać ramkę
  detachInterrupt(19);
  Serial1.begin(BAUD_RATE, SERIAL_8N1);
  Serial1.setTimeout(0);
  //czekamy 15ms, bo ramka trwa maksymalnie ~13ms
  delay(15);
  //Sprawdzamy ile bajtów odczytano
  uint8_t data_count = Serial1.available();
  uint8_t data[11] = {0};
  //zabieramy bajty z bufora UARTu
  if(data_count == Serial1.readBytes(data, min(11,data_count)) && data_count > 1){
    //mamy ramkę, która zawiera conajmniej PID, zapisujemy ją
    data_frame newframe;
    dataToFrame(newframe, data, data_count);
    //czy już była czytana wcześniej w tej pętli ramek?
    bool neu = true;
    for(int i = 0; i < frame_loop_count; ++i){
      if(frame_loop[i] == newframe.pid){
        Serial.println(" EOL" + String(frame_loop_count));
        frame_loop[0] = newframe.pid;
        frame_loop_count = 1;
        neu = false;
        break;
      }
    }
    if(neu){
      frame_loop[frame_loop_count] = newframe.pid;
      ++frame_loop_count;
    }
    //Czy ostatnio odczytane wartości były takie same?
    neu = true;
    for(int i = 0; i < saved_frames_count; ++i){
      //Jeśli była już taka czytana LUB jeśli wymuszone jest ignorowanie ramek diagnostycznych...
      if(memcmp(&newframe, FRAME_MEMORY+i, sizeof(data_frame)) == 0 || ((newframe.pid == 0x3c || newframe.pid == 0x3d) && !digitalRead(IGNORE_DIAG))){
        //Można pominąć wypisywanie tej ramki, bo się nie zmieniła ani nie jest nowa
        Serial.print(HexToString(newframe.pid) + "/ ");
        neu = false;
        break;
      }
    }
    if(neu){
      printFrame(newframe, data_count>=3);
      //dodaj ramkę do listy - najpierw sprawdź, czy już nie było takiego PID
      for(int i = 0; i < saved_frames_count; ++i){
        if(FRAME_MEMORY[i].pid == newframe.pid){
          memcpy(FRAME_MEMORY+i, &newframe, sizeof(data_frame));
          neu = false;
          break;
        }
      }
      if(neu){
        memcpy(FRAME_MEMORY+saved_frames_count, &newframe, sizeof(data_frame));
        ++saved_frames_count;
      }
    }
    

  }
  else{
    Serial.print("ERR ");
  }
  Serial1.end();
}