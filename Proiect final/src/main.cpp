#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "ScopeEngine.h"

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
uint16_t displaySnapshot[GRAPH_WIDTH]; 
int gridY[3]; 
uint16_t signalColor = TFT_GREEN; 

// Variabile Trigger
const int triggerLevel = 2000; 

// --- Configurare Generator Semnal ---
enum SignalType {
  SIGNAL_SQUARE,
  SIGNAL_SINE
};
SignalType currentSignalType = SIGNAL_SQUARE;
unsigned long lastModeChangeTime = 0;
const unsigned long modeChangeInterval = 6000; 

// Generator Vars
unsigned long lastSignalTime = 0;
const unsigned long signalHalfPeriod = 833; // nu mai e folosit direct
bool squareState = false;

// sineIndex float pentru precizie la incrementare (10.666)
float sineIndex = 0.0;
uint8_t sineTable[256];

// --- Variabile State Machine Osciloscop ---
enum ScopeState {
  STATE_SEARCHING,
  STATE_DRAWING
};
ScopeState scopeState = STATE_SEARCHING;
int drawIndex = 1;      
int triggerIndex = 0;   
int triggerMissedCount = 0; 

// --- Loop Regulator ---
const unsigned long LOOP_TARGET_MICROS = 61; 

// --- Parser Comenzi Serial (Non-Blocking) ---
String serialBuffer = "";

void handleSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      serialBuffer.toLowerCase();
      
      if (serialBuffer.startsWith("color ")) {
        String col = serialBuffer.substring(6);
        if (col == "verde") signalColor = TFT_GREEN;
        else if (col == "galben") signalColor = TFT_YELLOW;
        else if (col == "alb") signalColor = TFT_WHITE;
        else if (col == "rosu") signalColor = TFT_RED;
        else if (col == "mov") signalColor = TFT_MAGENTA;
        
        Serial.print("Culoare schimbata in: ");
        Serial.println(col);
      }
      serialBuffer = ""; // Reset buffer
    } else {
      if (serialBuffer.length() < 20) { // Evitam buffer overflow
        serialBuffer += c;
      }
    }
  }
}

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
  pinMode(DAC_PIN, OUTPUT); 
  
  // --- TEST SIGNALS (PWM Hardware) ---
  // Frequency matched to DAC Generator: ~683 Hz
  
  // Pin 27 -> 30% Duty Cycle
  ledcSetup(0, 800, 8); 
  ledcAttachPin(27, 0);
  ledcWrite(0, 77); 

  // Pin 14 -> 50% Duty Cycle
  ledcSetup(1, 800, 8); 
  ledcAttachPin(14, 1);
  ledcWrite(1, 128); 

  // Pin 12 -> 70% Duty Cycle
  ledcSetup(2, 800, 8); 
  ledcAttachPin(12, 2);
  ledcWrite(2, 179); 
  
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  precomputeSine(); 
  drawGrid();       

  for(int i=0; i<GRAPH_WIDTH; i++) oldSignalBuffer[i] = GRAPH_CENTER;
  
  scopeInit();
  scopeStart();
  
  Serial.println("Scope Engine Started. PWM Test Signals (100, 200, 300 Hz) active.");
}

void loop() {
  unsigned long loopStart = micros();
  unsigned long nowMillis = millis();

  // --- 0. COMANDA SERIAL ---
  handleSerial();

  // --- 1. SCHIMBARE MOD ---
  if (nowMillis - lastModeChangeTime >= modeChangeInterval) {
    lastModeChangeTime = nowMillis;
    if (currentSignalType == SIGNAL_SQUARE) {
      currentSignalType = SIGNAL_SINE;
    } else {
      currentSignalType = SIGNAL_SQUARE;
    }
  }

  // --- 2. GENERATOR SEMNAL (Sincronizat cu Loop-ul de 61us) ---
  if (currentSignalType == SIGNAL_SQUARE) {
    static int squareTickCount = 0;
    squareTickCount++;
    
    // Perioada totala = 24 ticks (24 * 61us = 1464us) -> aprox 3 unde pe ecran
    // Jumatate = 12 ticks
    if (squareTickCount < 12) { 
        dacWrite(DAC_PIN, 50);
    } else if (squareTickCount < 24) {
        dacWrite(DAC_PIN, 205);
    } else {
        squareTickCount = 0;
        dacWrite(DAC_PIN, 50);
    }
  } 
  else {
    // Sinus: 256 pasi totali / 24 ticks = 10.666 increment per tick
    sineIndex += 10.666; 
    
    if (sineIndex >= 256.0) sineIndex -= 256.0;
    
    // Convertim la int doar pentru accesul in tabel
    dacWrite(DAC_PIN, sineTable[(int)sineIndex]);
  }

  // --- 3. OSCILOSCOP (State Machine) ---
  switch (scopeState) {
    case STATE_SEARCHING:
      if (scopeCheckTrigger()) {
        bool foundTrigger = false;
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
                   triggerIndex = i;
                   while (triggerIndex > 0 && oscilloscopeBuffer[triggerIndex] > triggerLevel) {
                       triggerIndex--;
                   }
                   foundTrigger = true;
                   triggerMissedCount = 0;
                   break;
               }
           }
        }
        
        if (!foundTrigger) triggerMissedCount++;

        if (foundTrigger || triggerMissedCount > 10) {
            if (!foundTrigger) triggerIndex = 0; 
            
            // --- SNAPSHOT: Copiem datele pentru a preveni tearing-ul ---
            for(int k=0; k<GRAPH_WIDTH; k++) {
                displaySnapshot[k] = oscilloscopeBuffer[triggerIndex + k];
            }
            
            drawIndex = 1; 
            scopeState = STATE_DRAWING; 
        }
      }
      break;

    case STATE_DRAWING:
      if (drawIndex < GRAPH_WIDTH) {
          int i = drawIndex;
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

          // Folosim SNAPSHOT-ul si Culoarea selectata
          int val1 = displaySnapshot[i - 1];
          int val2 = displaySnapshot[i];
          
          int newY1 = map(val1, 50, 4050, GRAPH_HEIGHT, 0);
          int newY2 = map(val2, 50, 4050, GRAPH_HEIGHT, 0);
          newY1 = constrain(newY1, 0, GRAPH_HEIGHT - 1);
          newY2 = constrain(newY2, 0, GRAPH_HEIGHT - 1);
          tft.drawLine(x1, newY1, x2, newY2, signalColor);
          
          oldSignalBuffer[i-1] = val1;
          drawIndex++;
      } else {
          // --- FIX BUG: Salvam ultimul punct ---
          oldSignalBuffer[GRAPH_WIDTH - 1] = displaySnapshot[GRAPH_WIDTH - 1];
          
          scopeState = STATE_SEARCHING;
      }
      break;
  }
  
  // --- 4. LOOP REGULATOR ---
  while ((micros() - loopStart) < LOOP_TARGET_MICROS) {
    // Asteptam
  }
}
