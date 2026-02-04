#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H

#include <Arduino.h>

// --- Setari ---
// Frecventa de update a DAC-ului.
// 100kHz = 10us intre puncte.
// Permite generarea de 10kHz Sinus cu 10 puncte, sau 1kHz cu 100 puncte (foarte fin).
#define DAC_UPDATE_FREQ 100000 

enum GenMode {
    GEN_OFF,
    GEN_SQUARE,
    GEN_SINE
};

// --- Control ---
void genInit(uint8_t dacPin);
void genSetMode(GenMode mode);
void genSetFrequency(uint32_t freqHz); // Seteaza frecventa semnalului generat

// --- Functii Interne (volatile pentru ISR) ---
extern volatile uint32_t gen_timer_period_ticks; // Cati tick-uri de 100kHz constituie o perioada
extern volatile GenMode gen_mode;

#endif
