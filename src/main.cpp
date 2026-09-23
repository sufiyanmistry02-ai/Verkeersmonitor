
#include <Arduino.h>
#include <avr/io.h>

const float afstandMeter = 0.6f;
const unsigned long debounceTijdMs = 30;
const unsigned long snelheidTimeoutMs = 10800;

// Ik stuur het viercijferige display direct aan.
const bool displayIngeschakeld = true;

// Ik ga voorlopig uit van een common-cathode-display.
const bool commonAnode = false;

uint8_t voertuigAantal = 0;

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


// Ik stel de seriële communicatie in op 9600 baud.
void uartBegin() {
    uint16_t baudWaarde = (F_CPU / (16UL * 9600UL)) - 1;

    UBRR0H = static_cast<uint8_t>(baudWaarde >> 8);
    UBRR0L = static_cast<uint8_t>(baudWaarde);

    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}


// Ik verstuur één teken via het UART-dataregister.
void uartSchrijfTeken(char teken) {
    while (!(UCSR0A & (1 << UDRE0))) {
    }

    UDR0 = teken;
}


// Ik verstuur tekst via de seriële verbinding.
void uartSchrijfTekst(const char *tekst) {
    while (*tekst != '\0') {
        uartSchrijfTeken(*tekst);
        tekst++;
    }
}


// Ik verstuur een nieuwe regel.
void uartNieuweRegel() {
    uartSchrijfTeken('\r');
    uartSchrijfTeken('\n');
}


// Ik verstuur een geheel getal als tekst.
void uartSchrijfGetal(uint16_t getal) {
    char buffer[6];
    uint8_t lengte = 0;

    do {
        buffer[lengte] = '0' + (getal % 10);
        getal /= 10;
        lengte++;
    } while (getal > 0);

    while (lengte > 0) {
        lengte--;
        uartSchrijfTeken(buffer[lengte]);
    }
}


// Ik verstuur de snelheid met één cijfer achter de komma.
void uartSchrijfSnelheid(float snelheid) {
    if (snelheid < 0.0f) {
        snelheid = 0.0f;
    }

    uint32_t tienden =
        static_cast<uint32_t>(snelheid * 10.0f + 0.5f);

    uint32_t geheel = tienden / 10;
    uint8_t decimaal = tienden % 10;

    if (geheel > 65535UL) {
        uartSchrijfTekst(">65535");
        return;
    }

    uartSchrijfGetal(static_cast<uint16_t>(geheel));
    uartSchrijfTeken(',');
    uartSchrijfTeken('0' + decimaal);
}


// Ik toon het voertuigaantal in de Serial Monitor.
void toonAantalSerieel() {
    uartSchrijfTekst("Aantal voertuigen: ");
    uartSchrijfGetal(voertuigAantal);
    uartNieuweRegel();
}


// Ik toon de snelheid in de Serial Monitor.
void toonSnelheidSerieel() {
    uartSchrijfTekst("Snelheid: ");
    uartSchrijfSnelheid(snelheidKmh);
    uartSchrijfTekst(" km/uur");
    uartNieuweRegel();
}


// Ik zet een registerbit hoog of laag.
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


// Ik toon het voertuigaantal binair op A0 t/m A3.
void toonAantalBinair() {
    PORTC = (PORTC & 0b11110000) | (voertuigAantal & 0x0F);
}


// Ik tel precies één voertuig na een afgeronde meting.
void telVoertuig() {
    voertuigAantal = (voertuigAantal + 1) % 16;

    toonAantalBinair();
    toonAantalSerieel();
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


// Ik werk het display bij zolang er een snelheid beschikbaar is.
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


// Ik start een meting alleen als er nog geen meting loopt.
void verwerkSensor1() {
    if (wachtOpSensor2) {
        return;
    }

    snelheidStartUs = micros();
    snelheidStartMs = millis();

    wachtOpSensor2 = true;
    snelheidBeschikbaar = false;

    alleCijfersUit();

    uartSchrijfTekst("Meting gestart");
    uartNieuweRegel();
}


// Ik rond de meting af en tel daarna één voertuig.
void verwerkSensor2() {
    if (!wachtOpSensor2) {
        return;
    }

    unsigned long tijdVerschilUs = micros() - snelheidStartUs;

    if (tijdVerschilUs == 0) {
        return;
    }

    float tijdSeconden = tijdVerschilUs / 1000000.0f;

    snelheidKmh = (afstandMeter / tijdSeconden) * 3.6f;

    berekenDisplayCijfers();

    wachtOpSensor2 = false;
    snelheidBeschikbaar = true;

    toonSnelheidSerieel();
    telVoertuig();
}


// Ik lees beide sensoren via het PIND-register.
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


// Ik stop een meting als sensor 2 te lang uitblijft.
void controleerTimeouts() {
    if (
        wachtOpSensor2 &&
        millis() - snelheidStartMs >= snelheidTimeoutMs
    ) {
        wachtOpSensor2 = false;
        snelheidBeschikbaar = false;

        uartSchrijfTekst("Meting verlopen: sensor 2 niet gedetecteerd");
        uartNieuweRegel();
    }
}


// Ik stel de ingangen en uitgangen in via AVR-registers.
void setup() {
    // Ik gebruik D2 en D3 als ingangen met interne pull-ups.
    DDRD &= ~((1 << PD2) | (1 << PD3));
    PORTD |= (1 << PD2) | (1 << PD3);

    // Ik gebruik A0 t/m A3 voor de vier binaire leds.
    DDRC |= 0b00001111;
    PORTC &= 0b11110000;

    if (displayIngeschakeld) {
        // D4 t/m D7: segmenten A t/m D.
        DDRD |= 0b11110000;

        // D8 t/m D11: segmenten E, F, G en DP.
        DDRB |= 0b00001111;

        // D12, D13, A4 en A5: de vier displayposities.
        DDRB |= (1 << PB4) | (1 << PB5);
        DDRC |= (1 << PC4) | (1 << PC5);

        alleCijfersUit();
        schrijfSegmenten(0);
    }

    uartBegin();

    voertuigAantal = 0;
    toonAantalBinair();

    snelheidBeschikbaar = false;
    wachtOpSensor2 = false;

    sensor1VorigRuw = !(PIND & (1 << PD2));
    sensor2VorigRuw = !(PIND & (1 << PD3));

    sensor1Stabiel = sensor1VorigRuw;
    sensor2Stabiel = sensor2VorigRuw;

    uartSchrijfTekst("Verkeersmonitor gestart");
    uartNieuweRegel();

    toonAantalSerieel();
}


// Ik blijf de sensoren, timeout en het display bijwerken.
void loop() {
    leesSensoren();
    controleerTimeouts();
    vernieuwDisplay();
}