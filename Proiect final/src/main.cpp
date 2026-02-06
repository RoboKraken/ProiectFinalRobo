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
int triggerLevel = 2000; 

// --- Configurare Generator Semnal ---
enum SignalType {
  SIGNAL_SQUARE,
  SIGNAL_SINE,
  SIGNAL_TRIANGLE,
  SIGNAL_SAWTOOTH
};
SignalType currentSignalType = SIGNAL_SINE;

// Parametri dinamici generator
float genFrequency = 650.0; 
float waveIncrement = 10.666; 
int squareHalfPeriodTicks = 12; 

// Index comun pentru formele de unda
float waveIndex = 0.0; 

// Tabele de valori (LUT)
uint8_t sineTable[256];
uint8_t triTable[256];
uint8_t sawTable[256];

// --- Variabile State Machine Osciloscop ---
enum ScopeState {
  STATE_SEARCHING,
  STATE_DRAWING
};
ScopeState scopeState = STATE_SEARCHING;
int drawIndex = 1;      
int triggerIndex = 0;   
int triggerMissedCount = 0; 

// Variabile Sistem Extra
String triggerMode = "AUTO"; 
float samplingRateKSPS = 50.0; 

enum TriggerState {
  TRIG_AUTO,
  TRIG_NORMAL, 
  TRIG_NONE    
};
TriggerState currentTriggerState = TRIG_AUTO;

// --- Loop Regulator ---
const unsigned long LOOP_TARGET_MICROS = 61; 
const float LOOP_FREQ = 1000000.0 / 61.0; 

// --- FUNCTII AUXILIARE ---

void updateGenFreq(float freq) {
    genFrequency = freq;
    waveIncrement = (256.0 * freq) / LOOP_FREQ;
    squareHalfPeriodTicks = (int)(LOOP_FREQ / (2.0 * freq));
    if (squareHalfPeriodTicks < 1) squareHalfPeriodTicks = 1;
}

void precomputeWaveforms() {
  for(int i=0; i<256; i++) {
    sineTable[i] = 128 + 100 * sin(i * 2 * PI / 256);
    float triVal;
    if (i < 128) triVal = -1.0 + (i / 64.0); 
    else triVal = 1.0 - ((i - 128) / 64.0); 
    triTable[i] = 128 + (int)(100 * triVal);
    float sawVal = -1.0 + (i / 128.0);
    sawTable[i] = 128 + (int)(100 * sawVal);
  }
}

void drawGrid() {
  tft.fillRect(0, GRAPH_HEIGHT + 1, SCREEN_WIDTH, SCREEN_HEIGHT - GRAPH_HEIGHT, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("V", 2, 2); 
  for (int v = 1; v <= 3; v++) {
    int adcVal = (v * 4095) / 3.3;
    int yPos = map(adcVal, 50, 4050, GRAPH_HEIGHT, 0);
    gridY[v-1] = yPos;
    tft.drawNumber(v, 5, yPos - 4);
    for (int x = GRAPH_OFFSET_X; x < SCREEN_WIDTH; x += 5) tft.drawPixel(x, yPos, TFT_DARKGREY);
  }
  tft.drawFastHLine(0, GRAPH_HEIGHT, 240, TFT_WHITE); 
  tft.drawString("0", GRAPH_OFFSET_X, GRAPH_HEIGHT + 5);
  float totalTimeMs = (float)GRAPH_WIDTH / samplingRateKSPS;
  tft.drawFastVLine(GRAPH_OFFSET_X + GRAPH_WIDTH/2, GRAPH_HEIGHT, 5, TFT_WHITE);
  tft.drawFloat(totalTimeMs / 2.0, 1, GRAPH_OFFSET_X + GRAPH_WIDTH/2 - 10, GRAPH_HEIGHT + 5);
  tft.drawFastVLine(SCREEN_WIDTH - 1, GRAPH_HEIGHT, 5, TFT_WHITE);
  tft.drawFloat(totalTimeMs, 1, SCREEN_WIDTH - 25, GRAPH_HEIGHT + 5);
  tft.drawString("ms", SCREEN_WIDTH - 15, SCREEN_HEIGHT - 10);
}

void printHelp() {
  Serial.println("\n===== LISTA COMENZI DISPONIBILE =====");
  Serial.println("gen [sine|sqr|tri|saw]  -> Schimba forma de unda a generatorului.");
  Serial.println("genf [20.0 - 650.0]     -> Seteaza frecventa generatorului in Hz.");
  Serial.println("samp [10.0 - 150.0]     -> Seteaza rata de esantionare in kSPS.");
  Serial.println("trig [on|off|auto]      -> Seteaza modul de sincronizare (Trigger).");
  Serial.println("triglev [0.0 - 3.3]     -> Seteaza pragul de trigger in Volti.");
  Serial.println("color [verde|rosu|galben|alb|mov] -> Schimba culoarea graficului.");
  Serial.println("help                    -> Afiseaza aceasta lista.");
  Serial.println("=====================================");
}

void printMenu() {
  Serial.println("\n--- STATUS SISTEM ---");
  float trigV = (triggerLevel * 3.3) / 4095.0;
  Serial.print("Trigger: "); Serial.print(triggerMode);
  Serial.print(" @ "); Serial.print(trigV, 1); Serial.println(" V");
  Serial.print("Sampling: "); Serial.print(samplingRateKSPS, 1); Serial.println(" kSPS");
  Serial.print("Generator: "); 
  switch(currentSignalType) {
    case SIGNAL_SQUARE:   Serial.print("SQR"); break;
    case SIGNAL_SINE:     Serial.print("SINE"); break;
    case SIGNAL_TRIANGLE: Serial.print("TRI"); break;
    case SIGNAL_SAWTOOTH: Serial.print("SAW"); break;
  }
  Serial.print(" @ "); Serial.print(genFrequency, 1); Serial.println(" Hz");
  Serial.println("Tastati 'help' pentru lista completa de comenzi.");
}

String serialBuffer = "";
void handleSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialBuffer.trim(); serialBuffer.toLowerCase();
      
      if (serialBuffer == "help") {
        printHelp();
      }
      else if (serialBuffer.startsWith("gen ")) {
        String mode = serialBuffer.substring(4);
        if (mode == "sqr") currentSignalType = SIGNAL_SQUARE;
        else if (mode == "sine") currentSignalType = SIGNAL_SINE;
        else if (mode == "tri") currentSignalType = SIGNAL_TRIANGLE;
        else if (mode == "saw") currentSignalType = SIGNAL_SAWTOOTH;
      }
      else if (serialBuffer.startsWith("genf ")) {
        float f = serialBuffer.substring(5).toFloat();
        if (f >= 20.0 && f <= 650.0) updateGenFreq(f);
      }
      else if (serialBuffer.startsWith("color ")) {
        String col = serialBuffer.substring(6);
        if (col == "verde") signalColor = TFT_GREEN;
        else if (col == "galben") signalColor = TFT_YELLOW;
        else if (col == "alb") signalColor = TFT_WHITE;
        else if (col == "rosu") signalColor = TFT_RED;
        else if (col == "mov") signalColor = TFT_MAGENTA;
      }
      else if (serialBuffer.startsWith("trig ")) {
        String t = serialBuffer.substring(5);
        if (t == "on") { currentTriggerState = TRIG_NORMAL; triggerMode = "NORMAL (ON)"; }
        else if (t == "off") { currentTriggerState = TRIG_NONE; triggerMode = "NONE (OFF)"; }
        else if (t == "auto") { currentTriggerState = TRIG_AUTO; triggerMode = "AUTO"; }
      }
      else if (serialBuffer.startsWith("triglev ")) {
        float valV = serialBuffer.substring(8).toFloat();
        if (valV >= 0.0 && valV <= 3.3) triggerLevel = (int)((valV * 4095.0) / 3.3);
      }
      else if (serialBuffer.startsWith("samp ")) {
        float rateK = serialBuffer.substring(5).toFloat();
        if (rateK >= 10.0 && rateK <= 150.0) { samplingRateKSPS = rateK; scopeSetRate((uint32_t)(rateK * 1000)); drawGrid(); }
      }
      
      if (serialBuffer != "help") printMenu(); 
      serialBuffer = ""; 
    } else if (serialBuffer.length() < 20) serialBuffer += c;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH); pinMode(DAC_PIN, OUTPUT); 
  ledcSetup(0, 683, 8); ledcAttachPin(27, 0); ledcWrite(0, 77); 
  ledcSetup(1, 683, 8); ledcAttachPin(14, 1); ledcWrite(1, 128); 
  ledcSetup(2, 683, 8); ledcAttachPin(12, 2); ledcWrite(2, 179); 
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  precomputeWaveforms(); 
  updateGenFreq(683.0); 
  drawGrid();       
  for(int i=0; i<GRAPH_WIDTH; i++) oldSignalBuffer[i] = GRAPH_CENTER;
  scopeInit(); scopeStart();
  printMenu();
}

void loop() {
  unsigned long loopStart = micros();
  handleSerial();
  if (currentSignalType == SIGNAL_SQUARE) {
    static int squareTickCount = 0;
    squareTickCount++;
    if (squareTickCount < squareHalfPeriodTicks) dacWrite(DAC_PIN, 50);
    else if (squareTickCount < squareHalfPeriodTicks * 2) dacWrite(DAC_PIN, 205);
    else { squareTickCount = 0; dacWrite(DAC_PIN, 50); }
  } else {
    waveIndex += waveIncrement; 
    if (waveIndex >= 256.0) waveIndex -= 256.0;
    int idx = (int)waveIndex;
    uint8_t outputVal = 128;
    if (currentSignalType == SIGNAL_SINE) outputVal = sineTable[idx];
    else if (currentSignalType == SIGNAL_TRIANGLE) outputVal = triTable[idx];
    else if (currentSignalType == SIGNAL_SAWTOOTH) outputVal = sawTable[idx];
    dacWrite(DAC_PIN, outputVal);
  }
  switch (scopeState) {
    case STATE_SEARCHING:
      if (scopeCheckTrigger()) {
        bool foundTrigger = false;
        if (currentTriggerState == TRIG_NONE) { triggerIndex = 0; foundTrigger = true; } 
        else {
            // --- 2. MOD AUTO sau NORMAL (ON) ---
            // Relaxam histerezisul pentru stabilitate mai buna
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
                       // Backtracking pentru a gasi punctul exact de trecere
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
        }
        bool shouldDraw = false;
        if (currentTriggerState == TRIG_NONE) shouldDraw = true; 
        else if (currentTriggerState == TRIG_AUTO) { shouldDraw = (foundTrigger || triggerMissedCount > 10); if (!foundTrigger && shouldDraw) triggerIndex = 0; } 
        else if (currentTriggerState == TRIG_NORMAL) shouldDraw = foundTrigger;
        if (shouldDraw) { for(int k=0; k<GRAPH_WIDTH; k++) displaySnapshot[k] = oscilloscopeBuffer[triggerIndex + k]; drawIndex = 1; scopeState = STATE_DRAWING; }
      }
      break;
    case STATE_DRAWING:
      if (drawIndex < GRAPH_WIDTH) {
          int i = drawIndex; int x1 = GRAPH_OFFSET_X + (i - 1); int x2 = GRAPH_OFFSET_X + i;
          int oldY1 = map(oldSignalBuffer[i-1], 50, 4050, GRAPH_HEIGHT, 0);
          int oldY2 = map(oldSignalBuffer[i], 50, 4050, GRAPH_HEIGHT, 0);
          oldY1 = constrain(oldY1, 0, GRAPH_HEIGHT - 1); oldY2 = constrain(oldY2, 0, GRAPH_HEIGHT - 1);
          tft.drawLine(x1, oldY1, x2, oldY2, TFT_BLACK);
          if (x2 % 5 == 0) for(int k=0; k<3; k++) tft.drawPixel(x2, gridY[k], TFT_DARKGREY);
          int val1 = displaySnapshot[i - 1]; int val2 = displaySnapshot[i];
          int newY1 = map(val1, 50, 4050, GRAPH_HEIGHT, 0); int newY2 = map(val2, 50, 4050, GRAPH_HEIGHT, 0);
          newY1 = constrain(newY1, 0, GRAPH_HEIGHT - 1); newY2 = constrain(newY2, 0, GRAPH_HEIGHT - 1);
          tft.drawLine(x1, newY1, x2, newY2, signalColor);
          oldSignalBuffer[i-1] = val1; drawIndex++;
      } else { oldSignalBuffer[GRAPH_WIDTH - 1] = displaySnapshot[GRAPH_WIDTH - 1]; scopeState = STATE_SEARCHING; }
      break;
  }
  while ((micros() - loopStart) < LOOP_TARGET_MICROS) { } 
}