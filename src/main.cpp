#include <Arduino.h>
#include <avr/io.h>

// Drukknop 1 zit op D2 (PD2).
// Drukknop 2 zit op D3 (PD3).
// De vier leds zitten op A0 t/m A3 (PC0 t/m PC3).

void setup() {
    // D2 en D3 instellen als ingangen.
    DDRD &= ~((1 << PD2) | (1 << PD3));

    // Interne pull-upweerstanden inschakelen.
    // Niet ingedrukt = 1, ingedrukt = 0.
    PORTD |= (1 << PD2) | (1 << PD3);

    // A0 t/m A3 instellen als uitgangen voor de leds.
    DDRC |= 0b00001111;

    // Alle vier de leds uitzetten.
    PORTC &= 0b11110000;
}

void loop() {
    // Lees de knoppen rechtstreeks via het PIND-register.
    bool knop1 = !(PIND & (1 << PD2));
    bool knop2 = !(PIND & (1 << PD3));

    // Knop 1 ingedrukt: led 1 aan.
    // Knop 2 ingedrukt: led 2 aan.
    // Beide ingedrukt: beide leds aan.
    // Geen knop ingedrukt: beide leds uit.
    PORTC = (PORTC & 0b11110000)
          | (knop1 ? (1 << PC0) : 0)
          | (knop2 ? (1 << PC1) : 0);
}