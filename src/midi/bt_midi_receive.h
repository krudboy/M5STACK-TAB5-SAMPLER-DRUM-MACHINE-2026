#pragma once
class BtMidiReceive{public: void noteOn(unsigned char ch,unsigned char note,unsigned char vel); void noteOff(unsigned char ch,unsigned char note); void controlChange(unsigned char ch,unsigned char cc,unsigned char value); void clock(); void start(); void stop();};
