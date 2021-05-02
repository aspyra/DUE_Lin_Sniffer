#include "Arduino.h"
#include "LIN_handler.h"
#include "DueFlashStorage.h"

#define BUFFER_SIZE 200

#define SERIAL_BAUD 115200 //the baudrate used when communicating with a computer

//message clr
#define C_RED "\e[1;31m"
#define C_GRN "\e[1;32m"
#define C_YLW "\e[1;33m"
#define C_BLU "\e[1;34m"
#define C_RST "\e[1;0m"

enum frame_option_t
{
    option_undefined = 0,
    option_never,
    option_change,
    option_always
};

struct config_t
{
    frame_option_t frame_verbosity[LIN_MEM_SIZE];
    bool stub;
    long baudrate;
    bool clr;
    bool chk;
};

config_t config; //configuration of the sniffer
bool if_newlined = true;
DueFlashStorage dueFlashStorage;

void saveSettings()
{
    byte mem[sizeof(config_t)];
    memcpy(mem, &config, sizeof(config_t));
    dueFlashStorage.write(4, mem, sizeof(config_t)); // write byte array to flash
    if (dueFlashStorage.read(0) != 0)
        dueFlashStorage.write(0, 0);
}

void startSniffing()
{
    LIN_sniffer::LIN_state = initialize;
}

void stopSniffing()
{
    detachInterrupt(LIN_RX);
    LIN_sniffer::reset();
}

void setColor(const char *color)
{
    if (config.clr)
        Serial.print(color);
}

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
    config.baudrate = baud;
    bool on = (LIN_sniffer::LIN_state != stopped);
    if (on)
        stopSniffing();
    LIN_sniffer::LIN_BAUD = baud;
    if (on)
        startSniffing();
}

void setFrameOption(const uint8_t id, const frame_option_t option)
{
    config.frame_verbosity[id] = option;
}

void setStub(bool state)
{
    config.stub = state;
}

void setChk(bool state)
{
    config.chk = state;
}

void setColoring(bool state)
{
    config.clr = state;
}

void MarkNewLoop(uint8_t frame)
{
    if (!if_newlined)
        Serial.print('\n');
    setColor(C_YLW);
    Serial.print("NL: ");
    if_newlined = false;
    setColor(C_RST);
}

void MarkNewFrame(data_frame &frame)
{
    switch (config.frame_verbosity[frame.id])
    {
    case option_never:
        //just the stub
        if (config.stub)
        {
            setColor(C_GRN);
            Serial.print(HexToString(frame.id));
            Serial.print("/ ");
            setColor(C_RST);
            if_newlined = false;
        }
        break;
    case option_change:
    case option_undefined:
    case option_always:
        setColor(C_GRN);
        if (!if_newlined)
            Serial.print('\n');
        Serial.print(HexToString(frame.id));
        Serial.print(" | ");
        for (int i = 0; i < frame.data_count; ++i)
        {
            Serial.print(HexToString(frame.data[i]));
            Serial.print(' ');
        }
        if (config.chk)
        {
            Serial.print('(');
            Serial.print(HexToString(frame.chk));
            Serial.println(')');
        }
        else
            Serial.print('\n');
        setColor(C_RST);
        if_newlined = true;
        break;
    }
}

void MarkChangedFrame(data_frame &frame, data_frame *old_frame)
{
    switch (config.frame_verbosity[frame.id])
    {
    case option_never:
        //just the stub
        if (config.stub)
        {
            setColor(C_BLU);
            Serial.print(HexToString(frame.id));
            Serial.print("/ ");
            setColor(C_RST);
            if_newlined = false;
        }
        break;
    case option_change:
    case option_undefined:
    case option_always:
        if (!if_newlined)
            Serial.print('\n');
        Serial.print(HexToString(frame.id));
        Serial.print(" | ");
        for (int i = 0; i < frame.data_count; ++i)
        {
            if (frame.data[i] != old_frame->data[i])
            {
                setColor(C_BLU);
                Serial.print(HexToString(frame.data[i]));
                setColor(C_RST);
            }
            else
            {
                Serial.print(HexToString(frame.data[i]));
            }
            Serial.print(' ');
        }
        if (config.chk)
        {
            Serial.print('(');
            if (frame.chk != old_frame->chk)
            {
                setColor(C_BLU);
                Serial.print(HexToString(frame.chk));
                setColor(C_RST);
            }
            else
                Serial.print(HexToString(frame.chk));
            Serial.println(')');
        }
        else
            Serial.print('\n');
        setColor(C_RST);
        if_newlined = true;
        break;
    }
}

void MarkUnchangedFrame(data_frame &frame)
{
    switch (config.frame_verbosity[frame.id])
    {
    case option_never:
    case option_change:
    case option_undefined:
        //just the stub
        if (config.stub)
        {
            Serial.print(HexToString(frame.id));
            Serial.print("/ ");
            if_newlined = false;
        }
        break;

    case option_always:
        if (!if_newlined)
            Serial.print('\n');
        Serial.print(HexToString(frame.id));
        Serial.print(" | ");
        for (int i = 0; i < frame.data_count; ++i)
        {
            Serial.print(HexToString(frame.data[i]));
            Serial.print(' ');
        }
        if (config.chk)
        {
            Serial.print('(');
            Serial.print(HexToString(frame.chk));
            Serial.println(')');
        }
        else
            Serial.print('\n');
        if_newlined = true;
        break;
    }
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
                        setColor(C_YLW);
                        Serial.print("Baudrate changed to ");
                        Serial.println(baud);
                        setColor(C_RST);
                    }
                    else
                    {
                        setColor(C_RED);
                        Serial.println("Specify baudrate between 1000 and 20000.");
                        setColor(C_RST);
                    }
                }
                else
                {
                    setColor(C_RED);
                    Serial.println("Specify baudrate between 1000 and 20000.");
                    setColor(C_RST);
                }
            }
            else if (len == 5 && !memcmp(command_word, "start", 5))
            {
                startSniffing();
                setColor(C_YLW);
                Serial.println("Starting sniffing the LIN bus...");
                setColor(C_RST);
            }
            else if (len == 4 && !memcmp(command_word, "stop", 4))
            {
                stopSniffing();
                setColor(C_YLW);
                Serial.println("Stopped sniffing the LIN bus.");
                setColor(C_RST);
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

                                setColor(C_YLW);
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
                                default:
                                    break;
                                }
                                setColor(C_RST);
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
                                            setColor(C_YLW);
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
                                            default:
                                                break;
                                            }
                                            setColor(C_RST);
                                        }
                                        else
                                        {
                                            setColor(C_RED);
                                            Serial.print(command_word);
                                            Serial.println(" is not a correct frame ID.");
                                            setColor(C_RST);
                                        }
                                    }
                                    else
                                    {
                                        setColor(C_RED);
                                        Serial.print(command_word);
                                        Serial.println(" is not a hexadecimal frame ID.");
                                        setColor(C_RST);
                                    }
                                    command_word = strtok(NULL, " ");
                                }
                        }
                        else
                        {
                            setColor(C_RED);
                            Serial.println("Specify IDs of frames or use toe option 'all'.");
                            setColor(C_RST);
                        }
                    }
                    else
                    {
                        setColor(C_RED);
                        Serial.println("Specify 'never'/'change'/'always' after the show command.");
                        setColor(C_RST);
                    }
                }
                else
                {
                    setColor(C_RED);
                    Serial.println("Specify 'never'/'change'/'always' after the show command, followed by 'all' or IDs of frames.");
                    setColor(C_RST);
                }
            }
            else if (len == 4 && !memcmp(command_word, "stub", 4))
            {
                command_word = strtok(NULL, " ");
                if (command_word != NULL)
                {
                    //OPTIONS: on / off
                    len = strlen(command_word);
                    if (len == 2 && !memcmp(command_word, "on", 2))
                    {
                        setStub(true);
                        setColor(C_YLW);
                        Serial.println("Message stubs are turned on.");
                        setColor(C_RST);
                    }
                    else if (len == 3 && !memcmp(command_word, "off", 3))
                    {
                        setStub(false);
                        setColor(C_YLW);
                        Serial.println("Message stubs are turned off.");
                        setColor(C_RST);
                    }
                    else
                        Serial.println("Please specify one of the stub options: 'on' or 'off'.");
                }
                else
                    Serial.println("Please specify stub option: 'on' or 'off'.");
            }
            else if (len == 8 && !memcmp(command_word, "checksum", 8))
            {
                command_word = strtok(NULL, " ");
                if (command_word != NULL)
                {
                    //OPTIONS: on / off
                    len = strlen(command_word);
                    if (len == 2 && !memcmp(command_word, "on", 2))
                    {
                        setChk(true);
                        setColor(C_YLW);
                        Serial.println("Checksum showing is turned on.");
                        setColor(C_RST);
                    }
                    else if (len == 3 && !memcmp(command_word, "off", 3))
                    {
                        setChk(false);
                        setColor(C_YLW);
                        Serial.println("Checksum showing is turned off.");
                        setColor(C_RST);
                    }
                    else
                        Serial.println("Please specify one of the checksum options: 'on' or 'off'.");
                }
                else
                    Serial.println("Please specify checksum option: 'on' or 'off'.");
            }
            else if (len == 5 && !memcmp(command_word, "color", 5))
            {
                command_word = strtok(NULL, " ");
                if (command_word != NULL)
                {
                    //OPTIONS: on / off
                    len = strlen(command_word);
                    if (len == 2 && !memcmp(command_word, "on", 2))
                    {
                        setColoring(true);
                        setColor(C_YLW);
                        Serial.println("Message coloring turned on.");
                        setColor(C_RST);
                    }
                    else if (len == 3 && !memcmp(command_word, "off", 3))
                    {
                        setColoring(false);
                        setColor(C_YLW);
                        Serial.println("Message coloring is turned off.");
                        setColor(C_RST);
                    }
                    else
                    {
                        setColor(C_RED);
                        Serial.println("Please specify one of the color options: 'on' or 'off'.");
                        setColor(C_RST);
                    }
                }
                else
                {
                    setColor(C_RED);
                    Serial.println("Please specify color option: 'on' or 'off'.");
                    setColor(C_RST);
                }
            }
            else if (len == 4 && !memcmp(command_word, "save", 4))
            {
                saveSettings();
                setColor(C_YLW);
                Serial.println("Settings saved.");
                setColor(C_RST);
            }
            else //error
            {
                setColor(C_RED);
                Serial.print("Unknown command: ");
                Serial.println(command_word);
                setColor(C_RST);
            }
        }
    }
}

void setup()
{
    LIN_sniffer::init(MarkNewLoop, MarkNewFrame, MarkChangedFrame, MarkUnchangedFrame);
    Serial.begin(115200);

    //config loading
    if (dueFlashStorage.read(0) == 0)
    {
        Serial.println("Loading settings...");
        byte mem[sizeof(config_t)];
        for (uint32_t i = 0; i < sizeof(config_t); ++i)
        {
            mem[i] = dueFlashStorage.read(4 + i);
        }
        memcpy(&config, mem, sizeof(config_t));
        setBaudrate(config.baudrate);
    }
    else
    {
        //default settings
        setBaudrate(9600);
        setChk(false);
        setStub(true);
        setColoring(false);
    }
    setColor(C_GRN);
    Serial.println("Ready.");
    setColor(C_RST);
}

void loop()
{
    parseSerial();
    LIN_sniffer::loop();
}
