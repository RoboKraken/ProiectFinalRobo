#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "ScopeEngine.h"
// #include "SignalGenerator.h" // Renuntam la generatorul complex

TFT_eSPI tft = TFT_eSPI();

// --- Configurare Pini ---
#define TFT_BL 22
#define DAC_PIN 25

// --- Configurare Grafica ---
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define GRAPH_WIDTH 220 
#define GRAPH_OFFSET_X 20 
#undef GRAPH_HEIGHT 
#define GRAPH_HEIGHT 200 
#define GRAPH_CENTER (GRAPH_HEIGHT / 2)

// --- Variabile Globale ---
uint16_t oldSignalBuffer[GRAPH_WIDTH];
int gridY[3]; 

// Variabile Trigger
const int triggerLevel = 2000; 

// --- Configurare Generator Semnal (In Loop) ---
enum SignalType {
  SIGNAL_SQUARE,
  SIGNAL_SINE
};
SignalType currentSignalType = SIGNAL_SQUARE;
unsigned long lastModeChangeTime = 0;
const unsigned long modeChangeInterval = 6000; 

// Generator Vars
unsigned long lastSignalTime = 0;
const unsigned long signalHalfPeriod = 833; // 600Hz
bool squareState = false;

const unsigned long sineStepInterval = 3; 
unsigned long lastSineStepTime = 0;
int sineIndex = 0;
uint8_t sineTable[256];

void precomputeSine() {
  for(int i=0; i<256; i++) {
    sineTable[i] = 128 + 100 * sin(i * 2 * PI / 256);
  }
}

void drawGrid() {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("V", 2, 2); 

  for (int v = 1; v <= 3; v++) {
    int adcVal = (v * 4095) / 3.3;
    int yPos = map(adcVal, 50, 4050, GRAPH_HEIGHT, 0);
    gridY[v-1] = yPos;
    tft.drawNumber(v, 5, yPos - 4);
    for (int x = GRAPH_OFFSET_X; x < SCREEN_WIDTH; x += 5) {
      tft.drawPixel(x, yPos, TFT_DARKGREY);
    }
  }

  tft.drawFastHLine(0, GRAPH_HEIGHT, 240, TFT_WHITE); 
  tft.drawString("0", GRAPH_OFFSET_X, GRAPH_HEIGHT + 5);
  
  float totalTimeMs = 4.4; 
  tft.drawFastVLine(GRAPH_OFFSET_X + GRAPH_WIDTH/2, GRAPH_HEIGHT, 5, TFT_WHITE);
  tft.drawFloat(totalTimeMs / 2.0, 1, GRAPH_OFFSET_X + GRAPH_WIDTH/2 - 10, GRAPH_HEIGHT + 5);
  tft.drawFastVLine(SCREEN_WIDTH - 1, GRAPH_HEIGHT, 5, TFT_WHITE);
  tft.drawFloat(totalTimeMs, 1, SCREEN_WIDTH - 25, GRAPH_HEIGHT + 5);
  tft.drawString("ms", SCREEN_WIDTH - 15, SCREEN_HEIGHT - 10);
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(DAC_PIN, OUTPUT); // Important pentru dacWrite initial
  
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  precomputeSine(); 
  drawGrid();       

  for(int i=0; i<GRAPH_WIDTH; i++) oldSignalBuffer[i] = GRAPH_CENTER;
  
  // Pornim doar Osciloscopul (DMA)
  scopeInit();
  scopeStart();
  
  Serial.println("Scope Engine Started. Generator running in Main Loop.");
}

void loop() {
  unsigned long now = micros();
  unsigned long nowMillis = millis();

  // --- 1. SCHIMBARE MOD ---
  if (nowMillis - lastModeChangeTime >= modeChangeInterval) {
    lastModeChangeTime = nowMillis;
    if (currentSignalType == SIGNAL_SQUARE) {
      currentSignalType = SIGNAL_SINE;
    } else {
      currentSignalType = SIGNAL_SQUARE;
    }
  }

  // --- 2. GENERATOR SEMNAL (Core 1 - Intercalat cu Draw) ---
  if (currentSignalType == SIGNAL_SQUARE) {
    // Logica veche "buna": Folosim sineStepInterval pentru granularitate fina
    // si toggluim manual intre 50 si 205
    if (now - lastSineStepTime >= sineStepInterval) {
        lastSineStepTime = now;
        
        // Verificam perioada dreptunghiulara (833us * 2 = 1666us total)
        // signalHalfPeriod e 833
        if(now - lastSignalTime < signalHalfPeriod) {
            squareState = false; // LOW (50)
        } else if (now - lastSignalTime < signalHalfPeriod * 2) {
            squareState = true;  // HIGH (205)
        } else {
            lastSignalTime = now; // Reset perioada
        }
        
        dacWrite(DAC_PIN, squareState ? 205 : 50);
    }
  } 
  else {
    if (now - lastSineStepTime >= sineStepInterval) {
      lastSineStepTime = now;
      sineIndex += 2; 
      if (sineIndex >= 256) sineIndex -= 256;
      dacWrite(DAC_PIN, sineTable[sineIndex]);
    }
  }

  // --- 3. OSCILOSCOP ---
  if (scopeCheckTrigger()) {
    int startIdx = 0;
    bool foundTrigger = false;
    static int triggerMissedCount = 0;
    const int hyst = 50; 
    const int HYST_WINDOW = 8;
    
    for (int i = HYST_WINDOW + 1; i < ADC_BUFFER_SIZE / 2; i++) {
       if (oscilloscopeBuffer[i] > (triggerLevel + hyst)) {
           int below_count = 0;
           for (int w = 1; w <= HYST_WINDOW; w++) {
               if (oscilloscopeBuffer[i - w] < (triggerLevel - hyst)) {
                   below_count++;
               }
           }
           if (below_count >= HYST_WINDOW / 2) {
               startIdx = i;
               while (startIdx > 0 && oscilloscopeBuffer[startIdx] > triggerLevel) {
                   startIdx--;
               }
               foundTrigger = true;
               triggerMissedCount = 0;
               break;
           }
       }
    }
    
    if (!foundTrigger) triggerMissedCount++;

    if (foundTrigger || triggerMissedCount > 10) {
        if (!foundTrigger) startIdx = 0; 
        
        for (int i = 1; i < GRAPH_WIDTH; i++) {
            int x1 = GRAPH_OFFSET_X + (i - 1);
            int x2 = GRAPH_OFFSET_X + i;

            int oldY1 = map(oldSignalBuffer[i-1], 50, 4050, GRAPH_HEIGHT, 0);
            int oldY2 = map(oldSignalBuffer[i], 50, 4050, GRAPH_HEIGHT, 0);
            oldY1 = constrain(oldY1, 0, GRAPH_HEIGHT - 1);
            oldY2 = constrain(oldY2, 0, GRAPH_HEIGHT - 1);
            tft.drawLine(x1, oldY1, x2, oldY2, TFT_BLACK);

            if (x2 % 5 == 0) {
              for(int k=0; k<3; k++) tft.drawPixel(x2, gridY[k], TFT_DARKGREY);
            }

            int val1 = oscilloscopeBuffer[startIdx + i - 1];
            int val2 = oscilloscopeBuffer[startIdx + i];
            
            int newY1 = map(val1, 50, 4050, GRAPH_HEIGHT, 0);
            int newY2 = map(val2, 50, 4050, GRAPH_HEIGHT, 0);
            newY1 = constrain(newY1, 0, GRAPH_HEIGHT - 1);
            newY2 = constrain(newY2, 0, GRAPH_HEIGHT - 1);
            tft.drawLine(x1, newY1, x2, newY2, TFT_GREEN);
            
            oldSignalBuffer[i-1] = val1;
        }
        oldSignalBuffer[GRAPH_WIDTH-1] = oscilloscopeBuffer[startIdx + GRAPH_WIDTH - 1];
        if (!foundTrigger) triggerMissedCount = 0; 
    }
  }
}