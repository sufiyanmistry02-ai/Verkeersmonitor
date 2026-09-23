
#include <Arduino.h>
#include <avr/io.h>

const float afstandMeter = 0.6f;
const unsigned long debounceTijdMs = 30;
const unsigned long telTimeoutMs = 1000;
const unsigned long snelheidTimeoutMs = 10800;

// Ik stuur het viercijferige display direct aan.
const bool displayIngeschakeld = true;

// Ik ga voorlopig uit van een common-cathode-display.
const bool commonAnode = false;

uint8_t voertuigAantal = 0;

bool wachtOpTweedeAs = false;
unsigned long eersteAsTijdMs = 0;

bool wachtOpSensor2 = false;
unsigned long snelheidStartUs = 0;
unsigned long snelheidStartMs = 0;

float snelheidKmh = 0.0f;
bool snelheidBeschikbaar = false;

bool sensor1Stabiel = false;
bool sensor2Stabiel = false;

bool sensor1VorigRuw = false;
bool sensor2VorigRuw = false;

unsigned long sensor1LaatsteWijzigingMs = 0;
unsigned long sensor2LaatsteWijzigingMs = 0;

uint8_t displayCijfers[4] = {0, 0, 0, 0};
uint8_t actiefCijfer = 0;
unsigned long vorigeDisplayWisselUs = 0;

const unsigned long displayIntervalUs = 2000;

// Ik gebruik bitpatronen voor de cijfers 0 t/m 9.
const uint8_t cijferPatronen[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
};


// Ik zet een bit in een register hoog of laag.
void schrijfBit(
    volatile uint8_t &poort,
    uint8_t bit,
    bool hoog
) {
    if (hoog) {
        poort |= (1 << bit);
    } else {
        poort &= ~(1 << bit);
    }
}


// Ik toon het voertuigaantal binair op de vier leds.
void toonAantalBinair() {
    PORTC = (PORTC & 0b11110000) | (voertuigAantal & 0x0F);
}


// Ik verhoog de teller en begin na 15 opnieuw bij 0.
void telVoertuig() {
    voertuigAantal = (voertuigAantal + 1) % 16;
    toonAantalBinair();
}


// Ik splits de snelheid in drie cijfers voor het display.
void berekenDisplayCijfers() {
    uint16_t waarde;

    if (snelheidKmh >= 999.0f) {
        waarde = 999;
    } else if (snelheidKmh <= 0.0f) {
        waarde = 0;
    } else {
        waarde = static_cast<uint16_t>(snelheidKmh + 0.5f);
    }

    displayCijfers[0] = (waarde / 100) % 10;
    displayCijfers[1] = (waarde / 10) % 10;
    displayCijfers[2] = waarde % 10;
    displayCijfers[3] = 0;
}


// Ik schakel alle vier de displayposities uit.
void alleCijfersUit() {
    bool uitNiveau = commonAnode ? false : true;

    schrijfBit(PORTB, PB4, uitNiveau);
    schrijfBit(PORTB, PB5, uitNiveau);
    schrijfBit(PORTC, PC4, uitNiveau);
    schrijfBit(PORTC, PC5, uitNiveau);
}


// Ik stuur de segmenten aan via PORTD en PORTB.
void schrijfSegmenten(uint8_t patroon) {
    for (uint8_t segment = 0; segment < 8; segment++) {
        bool segmentAan = (patroon & (1 << segment)) != 0;
        bool niveau = commonAnode ? !segmentAan : segmentAan;

        if (segment < 4) {
            schrijfBit(PORTD, PD4 + segment, niveau);
        } else {
            schrijfBit(PORTB, PB0 + segment - 4, niveau);
        }
    }
}


// Ik selecteer één van de vier displayposities.
void selecteerCijfer(uint8_t positie) {
    bool aanNiveau = commonAnode ? true : false;

    switch (positie) {
        case 0:
            schrijfBit(PORTB, PB4, aanNiveau);
            break;

        case 1:
            schrijfBit(PORTB, PB5, aanNiveau);
            break;

        case 2:
            schrijfBit(PORTC, PC4, aanNiveau);
            break;

        case 3:
            schrijfBit(PORTC, PC5, aanNiveau);
            break;
    }
}


// Ik laat het display leeg tijdens de snelheidsmeting.
void vernieuwDisplay() {
    if (!displayIngeschakeld) {
        return;
    }

    if (!snelheidBeschikbaar || wachtOpSensor2) {
        alleCijfersUit();
        return;
    }

    unsigned long nuUs = micros();

    if (nuUs - vorigeDisplayWisselUs < displayIntervalUs) {
        return;
    }

    vorigeDisplayWisselUs = nuUs;

    alleCijfersUit();

    // Ik gebruik drie cijfers van mijn viercijferige display.
    if (actiefCijfer < 3) {
        uint8_t patroon =
            cijferPatronen[displayCijfers[actiefCijfer]];

        // Ik laat onnodige nullen aan de linkerkant weg.
        if (actiefCijfer == 0 && snelheidKmh < 100.0f) {
            patroon = 0;
        }

        if (actiefCijfer == 1 && snelheidKmh < 10.0f) {
            patroon = 0;
        }

        schrijfSegmenten(patroon);
        selecteerCijfer(actiefCijfer);
    }

    actiefCijfer = (actiefCijfer + 1) % 4;
}


// Ik start de snelheidsmeting bij de eerste puls van sensor 1.
void startSnelheidsmeting() {
    snelheidStartUs = micros();
    snelheidStartMs = millis();

    wachtOpSensor2 = true;
    snelheidBeschikbaar = false;

    alleCijfersUit();
}


// Ik verwerk een geldige puls van sensor 1.
void verwerkSensor1() {
    unsigned long nuMs = millis();

    if (!wachtOpTweedeAs) {
        eersteAsTijdMs = nuMs;
        wachtOpTweedeAs = true;

        startSnelheidsmeting();
        return;
    }

    // Ik tel één voertuig als beide assen binnen één seconde passeren.
    if (nuMs - eersteAsTijdMs < telTimeoutMs) {
        telVoertuig();
    }

    wachtOpTweedeAs = false;
}


// Ik bereken de snelheid wanneer sensor 2 wordt geactiveerd.
void verwerkSensor2() {
    if (!wachtOpSensor2) {
        return;
    }

    unsigned long tijdVerschilUs = micros() - snelheidStartUs;

    if (tijdVerschilUs > 0) {
        float tijdSeconden = tijdVerschilUs / 1000000.0f;

        snelheidKmh =
            (afstandMeter / tijdSeconden) * 3.6f;

        berekenDisplayCijfers();
        snelheidBeschikbaar = true;
    }

    wachtOpSensor2 = false;
}


// Ik lees beide sensoren rechtstreeks via het PIND-register.
void leesSensoren() {
    unsigned long nuMs = millis();

    bool sensor1Ruw = !(PIND & (1 << PD2));
    bool sensor2Ruw = !(PIND & (1 << PD3));

    // Ik filter het denderen van sensor 1.
    if (sensor1Ruw != sensor1VorigRuw) {
        sensor1LaatsteWijzigingMs = nuMs;
        sensor1VorigRuw = sensor1Ruw;
    }

    if (nuMs - sensor1LaatsteWijzigingMs >= debounceTijdMs) {
        if (sensor1Ruw != sensor1Stabiel) {
            sensor1Stabiel = sensor1Ruw;

            if (sensor1Stabiel) {
                verwerkSensor1();
            }
        }
    }

    // Ik filter het denderen van sensor 2.
    if (sensor2Ruw != sensor2VorigRuw) {
        sensor2LaatsteWijzigingMs = nuMs;
        sensor2VorigRuw = sensor2Ruw;
    }

    if (nuMs - sensor2LaatsteWijzigingMs >= debounceTijdMs) {
        if (sensor2Ruw != sensor2Stabiel) {
            sensor2Stabiel = sensor2Ruw;

            if (sensor2Stabiel) {
                verwerkSensor2();
            }
        }
    }
}


// Ik annuleer metingen wanneer de volgende puls te laat komt.
void controleerTimeouts() {
    unsigned long nuMs = millis();

    if (
        wachtOpTweedeAs &&
        nuMs - eersteAsTijdMs >= telTimeoutMs
    ) {
        wachtOpTweedeAs = false;
    }

    if (
        wachtOpSensor2 &&
        nuMs - snelheidStartMs >= snelheidTimeoutMs
    ) {
        wachtOpSensor2 = false;
        snelheidBeschikbaar = false;
    }
}


// Ik stel de ingangen en uitgangen in via de AVR-registers.
void setup() {
    // Ik gebruik D2 en D3 als ingangen met interne pull-ups.
    DDRD &= ~((1 << PD2) | (1 << PD3));
    PORTD |= (1 << PD2) | (1 << PD3);

    // Ik gebruik A0 t/m A3 voor de vier binaire leds.
    DDRC |= 0b00001111;
    PORTC &= 0b11110000;

    // Ik stel de displayuitgangen in.
    if (displayIngeschakeld) {
        // D4 t/m D7: segmenten A t/m D.
        DDRD |= 0b11110000;

        // D8 t/m D11: segmenten E, F, G en DP.
        DDRB |= 0b00001111;

        // D12 en D13: displayposities 1 en 2.
        DDRB |= (1 << PB4) | (1 << PB5);

        // A4 en A5: displayposities 3 en 4.
        DDRC |= (1 << PC4) | (1 << PC5);

        alleCijfersUit();
        schrijfSegmenten(0);
    }

    // Na een reset begint de teller opnieuw bij 0.
    voertuigAantal = 0;
    toonAantalBinair();

    snelheidBeschikbaar = false;

    sensor1VorigRuw = !(PIND & (1 << PD2));
    sensor2VorigRuw = !(PIND & (1 << PD3));

    sensor1Stabiel = sensor1VorigRuw;
    sensor2Stabiel = sensor2VorigRuw;
}


// Ik blijf de sensoren, timeouts en het display bijwerken.
void loop() {
    leesSensoren();
    controleerTimeouts();
    vernieuwDisplay();
}