#include "Arduino.h"
#include "LIN_handler.h"

#define SERIAL_BAUD 115200 //the baudrate used when communicating with a computer

String HexToString(const uint8_t byte, bool leading_zero = true)
{
    String temp = "";
    if (leading_zero && byte <= 0x0F)
        temp += "0";
    temp += String(byte, HEX);
    return temp;
}

String frameToString(data_frame frame, bool if_chk = true)
{
    String out = HexToString(frame.id) + " | ";
    for (int i = 0; i < frame.data_count; ++i)
        out += String(frame.data[i], HEX) + " ";
    if (if_chk)
        out += "(" + String(frame.chk, HEX) + ")";
    return out;
}

void MarkNewLoop(uint8_t frame){
    Serial.print("NL ");
};
void MarkNewFrame(data_frame &frame){
    Serial.print("NF ");
};
void MarkChangedFrame(data_frame &frame, data_frame *old_frame){
    Serial.print("CF ");
};
void MarkUnchangedFrame(data_frame &frame){
    Serial.print("UF ");
};


void setup()
{
    LIN_sniffer::begin(MarkNewLoop, MarkNewFrame, MarkChangedFrame, MarkUnchangedFrame);
    Serial.begin(115200);
}



void loop()
{
    LIN_sniffer::loop();
}