#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// --- Configurare Pini ---
#define TFT_BL 22
#define DAC_PIN 25
#define ADC_PIN 34

// --- Configurare Osciloscop ---
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define GRAPH_WIDTH 220 
#define GRAPH_OFFSET_X 20 
#undef GRAPH_HEIGHT 
#define GRAPH_HEIGHT 200 
#define GRAPH_CENTER (GRAPH_HEIGHT / 2)

// --- Variabile Globale ---
uint16_t signalBuffer[GRAPH_WIDTH]; 
uint16_t oldSignalBuffer[GRAPH_WIDTH];

// Pozitiile Y pentru liniile de 1V, 2V, 3V (calculate in setup)
int gridY[3]; 

// Stari Osciloscop
enum ScopeState {
  STATE_WAIT_TRIGGER,
  STATE_SAMPLING,
  STATE_DRAWING
};
ScopeState currentState = STATE_WAIT_TRIGGER;

// Variabile Sampling
int sampleIndex = 0;
unsigned long lastSampleTime = 0;
const int sampleRateUs = 50; 

// Variabile Trigger
const int triggerLevel = 2000; 
bool triggerReady = false; 
unsigned long triggerWaitStart = 0;
const unsigned long autoTriggerTimeout = 100; 

// --- Configurare Generator Semnal ---
enum SignalType {
  SIGNAL_SQUARE,
  SIGNAL_SINE
};
SignalType currentSignalType = SIGNAL_SQUARE;
unsigned long lastModeChangeTime = 0;
const unsigned long modeChangeInterval = 3000; 

// Dreptunghiular
unsigned long lastSignalTime = 0;
const unsigned long signalHalfPeriod = 5000; 
int squareState = 0;

// Sinusoidal
const unsigned long sineStepInterval = 78; 
unsigned long lastSineStepTime = 0;
int sineIndex = 0;
uint8_t sineTable[256];

void precomputeSine() {
  for(int i=0; i<256; i++) {
    sineTable[i] = 128 + 100 * sin(i * 2 * PI / 256);
  }
}

void drawGrid() {
  // Axa Y (Volti)
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("V", 2, 2); 

  for (int v = 1; v <= 3; v++) {
    int adcVal = (v * 4095) / 3.3;
    int yPos = map(adcVal, 50, 4050, GRAPH_HEIGHT, 0);
    
    // Salvam pozitia pentru reparare in loop
    gridY[v-1] = yPos;

    // Text (1, 2, 3)
    tft.drawNumber(v, 5, yPos - 4);
    
    // Linie punctata (Grila)
    for (int x = GRAPH_OFFSET_X; x < SCREEN_WIDTH; x += 5) {
      tft.drawPixel(x, yPos, TFT_DARKGREY);
    }
  }

  // Axa X (Timp)
  tft.drawFastHLine(0, GRAPH_HEIGHT, 240, TFT_WHITE); 
  tft.drawString("0", GRAPH_OFFSET_X, GRAPH_HEIGHT + 5);

  float totalTimeMs = (sampleRateUs * GRAPH_WIDTH) / 1000.0;
  
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
  pinMode(ADC_PIN, INPUT); 
  analogSetAttenuation(ADC_11db);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  precomputeSine(); 
  drawGrid();       

  for(int i=0; i<GRAPH_WIDTH; i++) oldSignalBuffer[i] = GRAPH_CENTER;
  
  triggerWaitStart = millis();
}

void loop() {
  unsigned long now = micros();
  unsigned long nowMillis = millis();

  // --- 0. SCHIMBARE MOD ---
  if (nowMillis - lastModeChangeTime >= modeChangeInterval) {
    lastModeChangeTime = nowMillis;
    if (currentSignalType == SIGNAL_SQUARE) {
      currentSignalType = SIGNAL_SINE;
    } else {
      currentSignalType = SIGNAL_SQUARE;
      pinMode(DAC_PIN, OUTPUT); 
    }
  }

  // --- 1. GENERATOR ---
  if (currentSignalType == SIGNAL_SQUARE) {
    if (now - lastSignalTime >= signalHalfPeriod) {
      lastSignalTime = now;
      squareState = (squareState == 0) ? 1 : 0;
      digitalWrite(DAC_PIN, squareState ? HIGH : LOW);
    }
  } 
  else {
    if (now - lastSineStepTime >= sineStepInterval) {
      lastSineStepTime = now;
      sineIndex++;
      if (sineIndex >= 256) sineIndex = 0;
      dacWrite(DAC_PIN, sineTable[sineIndex]);
    }
  }

  // --- 2. OSCILOSCOP ---
  switch (currentState) {
    case STATE_WAIT_TRIGGER: {
      int val = analogRead(ADC_PIN);
      if (millis() - triggerWaitStart > autoTriggerTimeout) {
         currentState = STATE_SAMPLING;
         sampleIndex = 0;
         lastSampleTime = micros();
         signalBuffer[0] = val; 
         sampleIndex++;
      }
      else {
        if (val < triggerLevel) {
          triggerReady = true; 
        } else if (triggerReady && val >= triggerLevel) {
          currentState = STATE_SAMPLING;
          sampleIndex = 0;
          lastSampleTime = micros();
          signalBuffer[0] = val; 
          sampleIndex++;
          triggerReady = false;
        }
      }
      break;
    }

    case STATE_SAMPLING: {
      if (now - lastSampleTime >= sampleRateUs) {
        lastSampleTime = now;
        signalBuffer[sampleIndex] = analogRead(ADC_PIN);
        sampleIndex++;

        if (sampleIndex >= GRAPH_WIDTH) {
          currentState = STATE_DRAWING;
        }
      }
      break;
    }

    case STATE_DRAWING: {
      for (int i = 1; i < GRAPH_WIDTH; i++) {
        int x1 = GRAPH_OFFSET_X + (i - 1);
        int x2 = GRAPH_OFFSET_X + i;

        // Sterge (cu negru)
        // PROTECTIE LINIE ALBA: Constrain la GRAPH_HEIGHT - 1
        int oldY1 = map(oldSignalBuffer[i-1], 50, 4050, GRAPH_HEIGHT, 0);
        int oldY2 = map(oldSignalBuffer[i], 50, 4050, GRAPH_HEIGHT, 0);
        oldY1 = constrain(oldY1, 0, GRAPH_HEIGHT - 1);
        oldY2 = constrain(oldY2, 0, GRAPH_HEIGHT - 1);
        tft.drawLine(x1, oldY1, x2, oldY2, TFT_BLACK);

        // REPARARE GRILA (Grid Repair)
        // Daca linia curenta x2 se suprapune cu punctele grilei (din 5 in 5 pixeli)
        if (x2 % 5 == 0) {
          for(int k=0; k<3; k++) {
             // Redesenam punctul de grila daca a fost sters
             // (Nu e nevoie sa verificam daca chiar a fost sters, e mai rapid sa il desenam oricum)
             tft.drawPixel(x2, gridY[k], TFT_DARKGREY);
          }
        }

        // Deseneaza NOU (cu verde)
        int newY1 = map(signalBuffer[i-1], 50, 4050, GRAPH_HEIGHT, 0);
        int newY2 = map(signalBuffer[i], 50, 4050, GRAPH_HEIGHT, 0);
        newY1 = constrain(newY1, 0, GRAPH_HEIGHT - 1);
        newY2 = constrain(newY2, 0, GRAPH_HEIGHT - 1);
        tft.drawLine(x1, newY1, x2, newY2, TFT_GREEN);
        
        oldSignalBuffer[i-1] = signalBuffer[i-1];
      }
      oldSignalBuffer[GRAPH_WIDTH-1] = signalBuffer[GRAPH_WIDTH-1];

      currentState = STATE_WAIT_TRIGGER;
      triggerWaitStart = millis();
      break;
    }
  }
}