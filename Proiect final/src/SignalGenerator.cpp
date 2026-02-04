#include "SignalGenerator.h"
#include <math.h>
#include <esp_task_wdt.h>

// --- Variabile Globale ---
volatile GenMode gen_mode = GEN_OFF;
uint8_t dac_pin = 25;

// Parametri Generator
volatile uint32_t gen_period_micros = 1000; 
volatile uint32_t half_period_micros = 500;

// Tabela Sinus
uint8_t sine_lut[256];
volatile int sine_step_micros = 10;

TaskHandle_t genTaskHandle;

// --- Task Generator (Core 0) ---
void genTask(void *pvParameters) {
    // Scoatem acest task de sub supravegherea Watchdog-ului (daca era)
    esp_task_wdt_delete(NULL);
    
    unsigned long lastMicros = micros();
    unsigned long lastWdtFeed = millis();
    int sineIdx = 0;
    bool squareState = false;
    
    while (1) {
        unsigned long now = micros();
        
        // Safety Valve: O data la 500ms, lasam sistemul sa respire 1 tick (1ms)
        // Asta previne resetarea Watchdog-ului pentru IDLE task.
        // Glitch-ul rezultat este de 1ms la fiecare 0.5s (imperceptibil vizual pe osciloscopul nostru)
        if (millis() - lastWdtFeed > 500) {
            vTaskDelay(1);
            lastWdtFeed = millis();
        }

        if (gen_mode == GEN_OFF) {
            vTaskDelay(10); 
            continue;
        }

        if (gen_mode == GEN_SQUARE) {
            if (now - lastMicros >= half_period_micros) {
                lastMicros = now;
                squareState = !squareState;
                // Folosim dacWrite pentru consistenta
                dacWrite(dac_pin, squareState ? 255 : 0);
            }
        } 
        else if (gen_mode == GEN_SINE) {
            if (now - lastMicros >= sine_step_micros) {
                lastMicros = now;
                sineIdx++;
                if (sineIdx >= 256) sineIdx = 0;
                dacWrite(dac_pin, sine_lut[sineIdx]);
            }
        }
    }
}

void genInit(uint8_t pin) {
    dac_pin = pin;
    
    for(int i=0; i<256; i++) {
        sine_lut[i] = 128 + 100 * sin(i * 2 * PI / 256.0);
    }

    // Prioritate 5 (Medie). I2S are 20 (Max). IDLE are 0.
    xTaskCreatePinnedToCore(
        genTask, "GenTask", 2048, NULL, 5, &genTaskHandle, 0
    );
}

void genSetMode(GenMode mode) {
    gen_mode = mode;
}

void genSetFrequency(uint32_t freqHz) {
    if (freqHz == 0) return;
    
    gen_period_micros = 1000000 / freqHz;
    half_period_micros = gen_period_micros / 2;
    
    sine_step_micros = 1000000 / (freqHz * 256);
    if (sine_step_micros < 1) sine_step_micros = 1; 
}
