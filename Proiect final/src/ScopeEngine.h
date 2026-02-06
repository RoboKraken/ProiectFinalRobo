#ifndef SCOPE_ENGINE_H
#define SCOPE_ENGINE_H

#include <Arduino.h>
#include <driver/i2s.h>

// --- Configurare Buffer ---
// 512 sample-uri. La 50kHz -> 10ms de date.
// DMA-ul ESP32 accepta max 1024 per descriptor. Pentru Stereo (2x) avem nevoie de 512.
#define ADC_BUFFER_SIZE 512 

// --- Variabile Globale (Shared Memory) ---
// Bufferii Ping-Pong (umpluti de Core 0)
extern volatile uint16_t adc_buffer0[ADC_BUFFER_SIZE];
extern volatile uint16_t adc_buffer1[ADC_BUFFER_SIZE];

// Indexul bufferului care se scrie ACUM (0 sau 1)
extern volatile uint8_t active_buffer_id;

// Flag care anunta Core 1 ca un buffer s-a umplut complet
extern volatile bool adc_buffer_ready;

// Bufferul "Snapshotted" pentru procesare si afisare (folosit de Core 1)
extern uint16_t oscilloscopeBuffer[ADC_BUFFER_SIZE];

// --- Control ---
void scopeInit();
void scopeStart();
void scopeSetRate(uint32_t rate); // Functie noua
bool scopeCheckTrigger();


#endif
