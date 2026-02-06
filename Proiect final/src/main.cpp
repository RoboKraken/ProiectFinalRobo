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
bool showStats = false; 

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

// Parametri Generator
float genFrequency = 683.0; 
float waveIncrement = 0; 
uint32_t dacPeriodMicros = 25; // Default 40kHz

// Index comun
float waveIndex = 0.0; 

// Tabele LUT
uint8_t sineTable[256];
uint8_t triTable[256];
uint8_t sawTable[256];

// Variabile Sistem
String triggerMode = "AUTO"; 
float samplingRateKSPS = 50.0; 

enum TriggerState {TRIG_AUTO, TRIG_NORMAL, TRIG_NONE };
TriggerState currentTriggerState = TRIG_AUTO;

// --- FUNCTII AUXILIARE ---

void updateGenFreq(float freq) {
    genFrequency = freq;
    dacPeriodMicros = 25; 
    waveIncrement = (256.0 * freq) / 40000.0;
}

void precomputeWaveforms() {
  for(int i=0; i<256; i++) {
    sineTable[i] = 128 + 100 * sin(i * 2 * PI / 256);
    float triVal = (i < 128) ? -1.0 + (i/64.0) : 1.0 - ((i-128)/64.0);
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
  Serial.println("stats                   -> Toggle Vmin/Vmax/Vpp pe ecran.");
  Serial.println("help                    -> Afiseaza aceasta lista.");
  Serial.println("=====================================");
}

void printMenu() {
  Serial.println("\n--- STATUS SISTEM ---");
  float trigV = (triggerLevel * 3.3) / 4095.0;
  Serial.print("[TRIG] Mode: "); Serial.print(triggerMode);
  Serial.print(" | Level: "); Serial.print(trigV, 1); Serial.println(" V");
  
  Serial.print("[ADC]  Rate: "); Serial.print(samplingRateKSPS, 1); Serial.println(" kSPS");
  
  Serial.print("[GEN]  Wave: "); 
  switch(currentSignalType) {
    case SIGNAL_SQUARE:   Serial.print("DREPTUNGHIULAR (SQR)"); break;
    case SIGNAL_SINE:     Serial.print("SINUSOIDAL (SINE)"); break;
    case SIGNAL_TRIANGLE: Serial.print("TRIUNGHIULAR (TRI)"); break;
    case SIGNAL_SAWTOOTH: Serial.print("FERASTRAU (SAW)"); break;
  }
  Serial.print(" | Freq: "); Serial.print(genFrequency, 1); Serial.println(" Hz");
  
  Serial.print("[UI]   Stats: "); Serial.println(showStats ? "ON" : "OFF");
  
  Serial.println("--------------------------------");
  Serial.println("Tastati 'help' pentru lista completa de comenzi.");
}

String serialBuffer = "";
void handleSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialBuffer.trim(); serialBuffer.toLowerCase();
      bool processed = false;
      if (serialBuffer == "help") { printHelp(); processed = true; }
      else if (serialBuffer == "stats") { 
          showStats = !showStats; 
          if(!showStats) tft.fillRect(160, 0, 80, 40, TFT_BLACK); 
          processed = true; 
      }
      else if (serialBuffer.startsWith("gen ")) {
        String m = serialBuffer.substring(4);
        if (m == "sqr") currentSignalType = SIGNAL_SQUARE;
        else if (m == "sine") currentSignalType = SIGNAL_SINE;
        else if (m == "tri") currentSignalType = SIGNAL_TRIANGLE;
        else if (m == "saw") currentSignalType = SIGNAL_SAWTOOTH;
        processed = true;
      }
      else if (serialBuffer.startsWith("genf ")) {
        float f = serialBuffer.substring(5).toFloat();
        if (f >= 20 && f <= 650) { updateGenFreq(f); processed = true; }
      }
      else if (serialBuffer.startsWith("color ")) {
        String c = serialBuffer.substring(6);
        if (c == "rosu") signalColor = TFT_RED;
        else if (c == "verde") signalColor = TFT_GREEN;
        else if (c == "galben") signalColor = TFT_YELLOW;
        else if (c == "alb") signalColor = TFT_WHITE;
        else if (c == "mov") signalColor = TFT_MAGENTA;
        processed = true;
      }
      else if (serialBuffer.startsWith("samp ")) {
          float r = serialBuffer.substring(5).toFloat();
          if (r >= 10 && r <= 150) { samplingRateKSPS = r; scopeSetRate(r*1000); drawGrid(); processed = true; }
      }
      else if (serialBuffer.startsWith("triglev ")) {
          float v = serialBuffer.substring(8).toFloat();
          if (v >= 0 && v <= 3.3) { triggerLevel = (int)((v*4095)/3.3); processed = true; }
      }
      else if (serialBuffer.startsWith("trig ")) {
          String t = serialBuffer.substring(5);
          if (t == "on") { currentTriggerState = TRIG_NORMAL; triggerMode="ON"; }
          else if (t == "off") { currentTriggerState = TRIG_NONE; triggerMode="OFF"; }
          else if (t == "auto") { currentTriggerState = TRIG_AUTO; triggerMode="AUTO"; }
          processed = true;
      }
      
      if(processed) printMenu();
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
  precomputeWaveforms(); updateGenFreq(683.0); drawGrid();
  for(int i=0; i<GRAPH_WIDTH; i++) oldSignalBuffer[i] = GRAPH_CENTER;
  scopeInit(); scopeStart();
  Serial.println("System Started. Burst Mode.");
  printMenu();
}

void loop() {
  // === FAZA 1: BURST GENERATION ===
  unsigned long burstStart = millis();
  unsigned long nextSampleTime = micros();
  
  while (millis() - burstStart < 25) {
      if (micros() >= nextSampleTime) {
          nextSampleTime += dacPeriodMicros; 
          
          if (currentSignalType == SIGNAL_SQUARE) {
              waveIndex += waveIncrement;
              if (waveIndex >= 256.0) waveIndex -= 256.0;
              dacWrite(DAC_PIN, (waveIndex < 128) ? 50 : 205);
          } 
          else {
              waveIndex += waveIncrement;
              if (waveIndex >= 256.0) waveIndex -= 256.0;
              
              uint8_t val = 128;
              if (currentSignalType == SIGNAL_SINE) val = sineTable[(int)waveIndex];
              else if (currentSignalType == SIGNAL_TRIANGLE) val = triTable[(int)waveIndex];
              else if (currentSignalType == SIGNAL_SAWTOOTH) val = sawTable[(int)waveIndex];
              dacWrite(DAC_PIN, val);
          }
      }
  }

  // === FAZA 2: PROCESARE & UI ===
  handleSerial();

  if (scopeCheckTrigger()) {
      int startIdx = 0;
      bool found = false;
      static int missed = 0;

      if (currentTriggerState == TRIG_NONE) { found = true; }
      else {
          for (int i = 5; i < ADC_BUFFER_SIZE/2; i++) {
              if (oscilloscopeBuffer[i] > triggerLevel + 50) { 
                  int below = 0;
                  for(int w=1; w<=8; w++) if(oscilloscopeBuffer[i-w] < triggerLevel-50) below++;
                  if(below >= 4) { 
                      startIdx = i; 
                      while(startIdx>0 && oscilloscopeBuffer[startIdx]>triggerLevel) startIdx--;
                      found = true; missed = 0; break; 
                  }
              }
          }
          if(!found) missed++;
      }

      bool draw = false;
      if (currentTriggerState == TRIG_NONE) draw = true;
      else if (currentTriggerState == TRIG_AUTO) { draw = (found || missed > 5); if(!found) startIdx=0; }
      else if (currentTriggerState == TRIG_NORMAL) draw = found;

      if (draw) {
          float vMin=3.3, vMax=0, vPp=0;
          if (showStats) {
              int minRaw=4095, maxRaw=0;
              for(int k=0; k<GRAPH_WIDTH; k++) {
                  uint16_t val = oscilloscopeBuffer[startIdx+k];
                  if(val<minRaw) minRaw=val; 
                  if(val>maxRaw) maxRaw=val;
              }
              vMin = (minRaw*3.3)/4095.0;
              vMax = (maxRaw*3.3)/4095.0;
              vPp = vMax - vMin;
          }

          for (int i = 1; i < GRAPH_WIDTH; i++) {
              int x1 = GRAPH_OFFSET_X + (i-1); int x2 = GRAPH_OFFSET_X + i;
              
              int oldY1 = map(oldSignalBuffer[i-1], 50, 4050, GRAPH_HEIGHT, 0);
              int oldY2 = map(oldSignalBuffer[i], 50, 4050, GRAPH_HEIGHT, 0);
              tft.drawLine(x1, constrain(oldY1,0,199), x2, constrain(oldY2,0,199), TFT_BLACK);
              if (x2%5==0) for(int k=0; k<3; k++) tft.drawPixel(x2, gridY[k], TFT_DARKGREY);

              uint16_t val1 = oscilloscopeBuffer[startIdx + i - 1];
              uint16_t val2 = oscilloscopeBuffer[startIdx + i];
              int newY1 = map(val1, 50, 4050, GRAPH_HEIGHT, 0);
              int newY2 = map(val2, 50, 4050, GRAPH_HEIGHT, 0);
              tft.drawLine(x1, constrain(newY1,0,199), x2, constrain(newY2,0,199), signalColor);
              
              oldSignalBuffer[i-1] = val1;
          }
          oldSignalBuffer[GRAPH_WIDTH-1] = oscilloscopeBuffer[startIdx + GRAPH_WIDTH - 1];

          if (showStats) {
              tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(1);
              tft.setCursor(170, 5); tft.print("Min:"); tft.print(vMin);
              tft.setCursor(170, 15); tft.print("Max:"); tft.print(vMax);
              tft.setCursor(170, 25); tft.print("Vpp:"); tft.print(vPp);
          }
      }
  }
}