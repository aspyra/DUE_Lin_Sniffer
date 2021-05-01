#include "Arduino.h"
#include "LIN_handler.h"

#define BUFFER_SIZE 200

#define SERIAL_BAUD 115200 //the baudrate used when communicating with a computer

enum frame_option_t
{
    option_undefined = 0,
    option_never,
    option_change,
    option_always
};

String HexToString(const uint8_t byte, const bool leading_zero = true)
{
    String temp = "";
    if (leading_zero && byte <= 0x0F)
        temp += "0";
    temp += String(byte, HEX);
    return temp;
}

String frameToString(const data_frame frame, const bool if_chk = true)
{
    String out = HexToString(frame.id) + " | ";
    for (int i = 0; i < frame.data_count; ++i)
        out += String(frame.data[i], HEX) + " ";
    if (if_chk)
        out += "(" + String(frame.chk, HEX) + ")";
    return out;
}

void setBaudrate(const long baud)
{
    //TODO
}

void setFrameOption(const uint8_t id, const frame_option_t option)
{
    //TODO
}

void startSniffing()
{
    //TODO
}

void stopSniffing()
{
    //TODO
}

void MarkNewLoop(uint8_t frame)
{
    Serial.print("NL ");
}
void MarkNewFrame(data_frame &frame)
{
    Serial.print("NF ");
}
void MarkChangedFrame(data_frame &frame, data_frame *old_frame)
{
    Serial.print("CF ");
}
void MarkUnchangedFrame(data_frame &frame)
{
    Serial.print("UF ");
}

bool getCommand(char *buffer)
{
    static uint8_t cmd_buf[BUFFER_SIZE];
    static uint8_t pos = 0;
    while (Serial.available())
    {
        char c = Serial.read();
        cmd_buf[pos++] = c;
        if (c == '\r' || c == '\n')
        {
            cmd_buf[pos - 1] = '\0';
            pos = 0;
            strcpy(buffer, (char *)cmd_buf);
            return true;
        }
        //overflow protection
        if (pos == BUFFER_SIZE)
        {
            Serial.flush();
            pos = 0;
        }
    }
    return false;
}

void parseSerial()
{
    char buffer[BUFFER_SIZE];
    if (getCommand(buffer))
    {
        char *command_word;
        command_word = strtok(buffer, " ");
        if (command_word != NULL)
        {
            uint8_t len = strlen(command_word);
            //first word - options:
            if (len == 4 && !memcmp(command_word, "baud", 4))
            {
                command_word = strtok(NULL, " ");
                if (command_word != NULL)
                {
                    long baud = atoi(command_word);
                    if (baud >= 1000 && baud <= 20000)
                    {
                        setBaudrate(baud);
                        Serial.print("Baudrate changed to ");
                        Serial.println(baud);
                    }
                    else
                    {
                        Serial.println("Specify baudrate between 1000 and 20000.");
                    }
                }
                else
                    Serial.println("Specify baudrate between 1000 and 20000.");
            }
            else if (len == 5 && !memcmp(command_word, "start", 5))
            {
                startSniffing();
                Serial.println("Starting sniffing the LIN bus...");
            }
            else if (len == 4 && !memcmp(command_word, "stop", 4))
            {
                stopSniffing();
                Serial.println("Stopped sniffing the LIN bus.");
            }
            else if (len == 4 && !memcmp(command_word, "show", 4))
            {
                command_word = strtok(NULL, " ");
                if (command_word != NULL)
                {
                    //OPTIONS: never, change, always
                    frame_option_t option = option_undefined;
                    len = strlen(command_word);
                    if (len == 5 && !memcmp(command_word, "never", 5))
                        option = option_never;
                    else if (len == 6 && !memcmp(command_word, "change", 6))
                        option = option_change;
                    else if (len == 6 && !memcmp(command_word, "always", 6))
                        option = option_always;
                    if (option != option_undefined)
                    {
                        command_word = strtok(NULL, " ");
                        if (command_word != NULL)
                        {
                            len = strlen(command_word);
                            if (len == 3 && !memcmp(command_word, "all", 3))
                            {
                                for (uint8_t i = 0; i < LIN_MEM_SIZE; ++i)
                                    setFrameOption(i, option);
                                switch (option)
                                {
                                case option_never:
                                    Serial.println("All frames are set to never show.");
                                    break;
                                case option_change:
                                    Serial.println("All frames are set to show on change.");
                                    break;
                                case option_always:
                                    Serial.println("All frames are set to always show.");
                                    break;
                                }
                            }
                            else //ignore some frames
                                while (command_word != NULL)
                                {
                                    len = strlen(command_word);
                                    bool alphanumeric = true;
                                    for (uint8_t i = 0; i < len; ++i)
                                        alphanumeric &= isHexadecimalDigit(command_word[i]);
                                    if (alphanumeric)
                                    {
                                        long id = strtol(command_word, NULL, 16);
                                        if (((id >= 1 && id <= 0x3F) || (id == 0 && command_word[0] == '0')))
                                        {
                                            setFrameOption(id, option);
                                            Serial.print("Frame ID ");
                                            Serial.print(HexToString(id, HEX));
                                            switch (option)
                                            {
                                            case option_never:
                                                Serial.println(" is set to never show.");
                                                break;
                                            case option_change:
                                                Serial.println(" is set to show on change.");
                                                break;
                                            case option_always:
                                                Serial.println(" is set to always show.");
                                                break;
                                            }
                                        }
                                        else
                                        {
                                            Serial.print(command_word);
                                            Serial.println(" is not a correct frame ID.");
                                        }
                                    }
                                    else
                                    {
                                        Serial.print(command_word);
                                        Serial.println(" is not a hexadecimal frame ID.");
                                    }
                                    command_word = strtok(NULL, " ");
                                }
                        }
                        else
                            Serial.println("Specify IDs of frames or use toe option 'all'.");
                    }
                    else
                        Serial.println("Specify 'never'/'change'/'always' after the show command.");
                }
                else
                    Serial.println("Specify 'never'/'change'/'always' after the show command, followed by 'all' or IDs of frames.");
            }
            else
            {
                Serial.print("Unknown command: ");
                Serial.println(command_word);
            }
        }
    }
}

void setup()
{
    LIN_sniffer::begin(MarkNewLoop, MarkNewFrame, MarkChangedFrame, MarkUnchangedFrame);
    Serial.begin(115200);
}

void loop()
{
    parseSerial();
    LIN_sniffer::loop();
}

/*
        struct configuration
    {
        uint8_t frame_verbosity[LIN_MEM_SIZE];
        long buadrate;
    };
    */