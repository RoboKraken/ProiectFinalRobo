#2
Facem un osciloscop folosind esp32, cu mai multe moduri de setare pentru frecventa de esantionare, avand procesare de semnal astfel incat semnalul captat sa fie afisat corect pe display

#3
Bill of Materials:
ESP32 board
fire
panou de display mic, pana la ~ 200*200 pixeli
butoane sau joystick



folosim DAC intern esp32 pt generarea semnalului analogic pe care il afisam

#4
nu folosim tutorial extern

#5
 ##1 system boundary este breadboard-ul pe care sta esp-ul, eventual incluzand un generator de semnale daca in loc sa folosim dac-ul intern adaugam un generator extern. Totul e wired, nu avem comunicare in afara camerei

 ##2 inteligenta este in intregime in esp32
 ##3 timing-ul si setarea de sampling si procesarea de sample-uri pentru a fi afisate in mod folositor e cea mai grea problema tehnica
 ##4 minimum demo ar fi captarea unui semnal continuu in adc-ul esp-ului, mai apoi afisarea semnalului pe screen la nivelul sau de voltaj in range-ul 0-3.3v
 ##5 nu poate fi doar un tutorial deoarece reprezinta o solutie specifica pentru platforma esp32 cu o anumita arhitectura aleasa, trebuie sa avem functii non-blocante, mecanism de trigger pentru semnal pentru a fi afisat corect pe ecran. Tutorialele sunt facute pentru componente individuale precum adc-ul, dma-ul si timere, dar integrarea lor in sistem depaseste un tutorial simplu si ar intra mai degraba in nivelul unui tutorial complex de cateva ore.
 ##6 da, este necesar ca hardware peste arduino deoarece arduino nu are capabilitatea necesara dma sau dac pentru proiect


 # Documentatie display
 Modul display color IPS de 1.3 inch, compatibil Arduino si alte placi de dezvoltare. Atentie! Suporta tensiune de 3.3V, atat logica cat si de alimentare. Folositi un convertor de nivel logic daca este necasara folosirea cu placi pe 5V.

Acest afisaj comunica prin SPI iar pixelii sunt controlabili individual, avand o rezolutie de 240x240 pixeli de tip RGB. Ecranul LCD este unul TFT cu culori realiste si IPS, lucru ce permite vizualizarea corecta a acestuia din toate unghiurile.

Dispune de retroiluminare de fundal controlabila prin pinul dedicat BLK (Low Level este OFF).

 

Specificatii:
Tensiune alimentare: 3.3VDC

Tensiune logica: 3.3VDC (NU este compatibil cu 5V! Folisiti convertor de nivel logic)

Integrat: ST7789VW

Numar pini: 7

Comunicare: SPI

Dimensiune ecran: 1.3 inch

Rezolutie: 240 x 240 pixeli

Culoare: RGB

Tip: LCD TFT, IPS

Temperatura operare: -20 - +70 grade C, fara condens

Dimensiuni mm: 39 x 28mm

 

Conectare:
GND: Ground, minus

VCC: 3.3VDC, plus

SCL: Pin SCL, Serial Clock

SDA: Pin SDA, Serial Data

RES: Pin Reset

DC: Pin Data Control

BLK: Pin control retroiluminare (backlight)

 

Utilizare:
Exemplul de utilizare este dat cu o placa ESP32 (nu necesita convertor de nivel logic, operand la 3.3V) conectata la afisaj, utilizand mediul Arduino IDE.

Instaleaza biblioteca TFT_eSPI_ES32Lab by Bodmer.