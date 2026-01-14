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