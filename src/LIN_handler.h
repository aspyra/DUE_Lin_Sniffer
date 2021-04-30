#pragma once
#include "Arduino.h"

//these two defines choose which serial port of the Due is used.
//They need to match!
#define LINSerial Serial1
#define LIN_RX 19

#define LIN_BAUD 9600

#define LIN_MEM_SIZE 64 //Defines the maximum number of unique frame indentifiers saved (only 64 possible)

//max frame time
//According to: https://www.cs-group.de/wp-content/uploads/2016/11/LIN_Specification_Package_2.2A.pdf
//T(header)max = 1.4 * 20 * T(bit) [we are excluding a 14-bit header break]
//T(data)max = 1.4 * 10 * (N(bytes) + 1) * T(bit)
//Assuming N(bytes) = 8 (max for LIN)
//T(frame)max = T(bit) * 154 = 154000000L / LIN_BAUD
//for 9600 baud, that is 16ms
#define LIN_MAX_FRAME_TIME 154000000L / LIN_BAUD

//the break field is at least the length of 11 bits
#define LIN_MIN_BREAK_TIME 11000000L / LIN_BAUD

//enum used to differenciate between states of LIN reception
enum LIN_mode_t
{
    waiting_for_break = 0, //waiting for break sign
    measuring_break,       //measuring the length of the break field
    reading_bytes          //reading serial bytes
};

//structure to conveniently store the frames
struct data_frame
{
    uint8_t id = 0x00;      //the ID of the frame (don't mix up with PID)
    uint8_t data_count = 0; //number of bytes in the data section
    uint8_t data[8] = {0};  //data carried by the frame
    uint8_t chk = 0x00;     //checksum
};

namespace LIN_sniffer
{

    //volatile variables accessed from an interrupt
    volatile LIN_mode_t LIN_mode;
    volatile unsigned long break_time;

    //global variables
    data_frame FRAME_MEMORY[LIN_MEM_SIZE]; //stores the last received instance of each frame id
    uint8_t frame_loop[LIN_MEM_SIZE];      //stores which frame ids have been received in this schedule loop. Duplicate id - new loop
    uint8_t frame_loop_count;              //stores how many different ids were received in this loop
    uint8_t saved_frames_count;            //stores how many different ids are stored in FRAME_MEMORY

    //function pointers to be defined by the user!
    void (*MarkNewLoop)(uint8_t);
    void (*MarkNewFrame)(data_frame &frame);
    void (*MarkChangedFrame)(data_frame &frame, data_frame *old_frame);
    void (*MarkUnchangedFrame)(data_frame &frame);

    //functions
    void LIN_RX_interrupt()
    {
        //depending on the actual LIN state and pin state, proceed to different state
        switch (LIN_mode)
        {
        case waiting_for_break:
            //if a falling edge is detected, start measuring how long the break field is
            if (!digitalRead(LIN_RX))
            {
                break_time = micros();
                LIN_mode = measuring_break;
            }
            return;

        case measuring_break:
            if (digitalRead(LIN_RX))
            {
                //the break field is at least the length of 11 bits
                if (micros() - break_time >= LIN_MIN_BREAK_TIME)
                {
                    //for reading the bytes, the interrupt is not needed
                    LIN_mode = reading_bytes;
                    detachInterrupt(digitalPinToInterrupt(LIN_RX));
                }
                else //if the length is too short - it could be a glitch or something went wrong. Try again until a correct break is received
                    LIN_mode = waiting_for_break;
            }
            return;
        default:
            return; //this should not be reached.
                    //the interrupt is turned off when reading bytes!
        }
    };
    void dataToFrame(data_frame &frame, uint8_t *data, uint8_t data_count)
    {
        frame.id = data[1] & 0x3F;
        if (data_count >= 3)
            frame.chk = data[data_count - 1];
        if (data_count >= 4)
        {
            frame.data_count = data_count - 3;
            memcpy(frame.data, data + 2, frame.data_count);
        }
    }
    void begin(void (*_MarkNewLoop)(uint8_t) = nullptr, void (*_MarkNewFrame)(data_frame &) = nullptr, void (*_MarkChangedFrame)(data_frame &, data_frame *) = nullptr, void (*_MarkUnchangedFrame)(data_frame &) = nullptr)
    {
        MarkNewLoop = _MarkNewLoop;
        MarkNewFrame = _MarkNewFrame;
        MarkChangedFrame = _MarkChangedFrame;
        MarkUnchangedFrame = _MarkUnchangedFrame;
        pinMode(LIN_RX, INPUT_PULLUP);
        frame_loop_count = 0;
        saved_frames_count = 0;
    }
    void loop()
    {
        //first: activer the interrupt
        LIN_mode = waiting_for_break;
        attachInterrupt(LIN_RX, LIN_RX_interrupt, CHANGE);

        while (LIN_mode != reading_bytes)
            ; //the interrupt handles receiving of the break signal

        //after the break signal is received, quickly turn on serial communication
        LINSerial.begin(LIN_BAUD, SERIAL_8N1);
        LINSerial.setTimeout(0);

        //wait a calculated number of time
        delay(LIN_MAX_FRAME_TIME);

        //check how many bytes have been received
        uint8_t data_count = LINSerial.available();
        uint8_t data_count_read;
        uint8_t data[11] = {0}; //eleven bytes is maximum (sync + pid + 8 bytes + chk)

        data_count_read = LINSerial.readBytes(data, min(11, data_count));
        //the serial communication can be stopped
        LINSerial.end();

        //analyse the received data

        if (data_count_read == data_count && data_count > 1) //we need at least sync + pid!
        {
            //we have at least pid, save this frame
            data_frame newframe;
            dataToFrame(newframe, data, data_count);
            //was this id of frame received in this loop?
            bool is_new = true;
            for (int i = 0; i < frame_loop_count; ++i)
            {
                //if this id was already received in this loop - start the loop over again
                if (frame_loop[i] == newframe.id)
                {
                    MarkNewLoop(frame_loop_count);
                    frame_loop[0] = newframe.id;
                    frame_loop_count = 1;
                    is_new = false;
                    break;
                }
            }
            //if the frame was not received in this loop - store it
            if (is_new)
            {
                frame_loop[frame_loop_count] = newframe.id;
                ++frame_loop_count;
            }

            //Have the exact frame be already received, or one with same pid?
            is_new = true;
            for (int i = 0; i < saved_frames_count; ++i)
            {
                //look for frame with the same id
                if (newframe.id == FRAME_MEMORY[i].id)
                {
                    is_new = false;
                    //if id is the same, what about the contents? - act accordingly
                    if (memcmp(&newframe, FRAME_MEMORY + i, sizeof(data_frame)) == 0)
                        MarkUnchangedFrame(newframe);
                    else
                    {
                        //we need to save the new values!
                        MarkChangedFrame(newframe, FRAME_MEMORY + i);
                        memcpy(FRAME_MEMORY + i, &newframe, sizeof(data_frame));
                    }
                    break;
                }
            }

            //if the frame was not received before - save it
            if (is_new)
            {
                MarkNewFrame(newframe);
                //add frame to the list
                memcpy(FRAME_MEMORY + saved_frames_count, &newframe, sizeof(data_frame));
                ++saved_frames_count;
            }
        }
    }
};