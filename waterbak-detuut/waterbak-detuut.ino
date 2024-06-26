/****************************************************************************************************
  Deze code bestuurt een maquette ('waterbak') bij stoomgemaal 'De Tuut' in Appeltern.
  Het is de bedoeling dat de maquette simuleert wat de jaargetijden en het weer (regen) doen met het
  water nivau in de rivieren de 'Maas', de 'Waal' en de wetering om het gemaal.
  Als het water niveau in de wetering te hoog wordt, gaat het gemaal pompen. Hierbij wordt het water
  van de wetering de 'Maas' in gepompt.

  Auteur: Edwin Spil

  Versie: 1.0 - Initiele implemenatie van een state machine.
  Versie: 1.1 - Toevoegen van de echte states en transities.
  Versie: 1.2 - Schema geupdate met pinout van maquete.
  Versie: 1.3 - Schakelaars in schema eindelijk goed aangesloten (zonder kortsluiting)
                Enums toegevoegd voor vlotter en standenschakelaar.
                Mega domme fout met return type gevonden met transities en deze gefixed.
  Versie: 1.4 - Code voor waterniveau sensor toegevoegd.
  Versie: 1.5 - Ontdenderen van de Stap knop en minimale stap tijd toegevoegd
  Versie: 1.6 - Minimale stap tijd aangepast en automatisch stappen toegevoegd
  Versie: 1.7 - Timers voor de hand en auto stand worden nu goed gereset
  Versie: 1.8 - Minimale stap timer werkt nu wel helemaal goed
                Stap led wordt nu goed aangestuurd
  Versie: 1.9 - Timer hulpvariabelen aangepast van int naar unsigned long
  Versie: 2.0 - Code voor Adafruit SoundBoard toegevoegd
                Overgang WinterRegen->GemaalAan veranderd van waterniveau naar HAND en AUTO timeout omdat het anders niet regent

****************************************************************************************************/

/* ---- Innemen van eingen gedefinieerde libraries ---- */
#include "enumeraties.h"

/* ---- Innemen van standatd libraries ---- */
#include <StateMachine.h>
#include <neotimer.h>
#include <Bounce2.h>
#include <Adafruit_Soundboard.h>

/* ---- Definieren van namen voor de pin nummers ----*/
// Soundboard hulp pinnen
#define SB_Actief 4  // Verbind deze met ACT pin op SoundBoard
#define SB_Reset 23  // Verbind deze met RST pin op SoundBoard
// Vlotter in watertank
#define Vlotter 27
// Standen schakelaar
#define SSHand 29
#define SSAuto 30
// Stap knop
#define Stap 50
#define LedStap 42
// Kleppen
#define K1 32  // Klep 1 - Leegloop Maas + Waal + Wetering
#define K2 33  // Klep 2 - Zomerstand Maas + Waal
#define K3 34  // Klep 3 - Regen
#define K4 35  // Klep 4 - Extra water Maas + Waal + Wetering
//#define K5 36  Klep 5 - Uiteindelijk niet geplaatst in maquette
#define K6 37  // Klep 6 - Zomerstand Wetering
// Pompen
#define P1 38  // Pomp 1 - Water Maas + Waal + Wetering
#define P2 48  // Pomp 2 - Gemaal
// Water Niveau Sensor
#define WaterNiveauSensor A2

/* ---- Debug via serial console hulp waardes ---- */
const bool SERIAL_DEBUG = false;  // zet deze op true en gebruik Serial Monitor voor debug
Neotimer debugTimer = Neotimer(500);

/* ---- AdaFruit SoundBoard hulp waardes ---- */
const bool SOUNDBOARD_AAN = true;
Adafruit_Soundboard soundboard = Adafruit_Soundboard(&Serial1, NULL, SB_Reset);

/* ---- Definieren van de minimale en maximale waarde voor mapping van de water niveau sensor ---- */
// De retourwaarde van de sensor is noraal tusson 0 en 1023 (niet tot volledig bedekt met water)
const int WATER_SENSOR_MIN = 0;
const int WATER_SENSOR_MAX = 630;

/* ---- Definieren van het waterniveau percentage waarbij het gemaal aan en uit moet ---- */
const int GEMAAL_AAN_PERCENTAGE = 10;
const int GEMAAL_UIT_PERCENTAGE = 69;

/* ---- Definieren van de (minimale) tijd (miliseconde) die een stap moet duren ---- */
const unsigned long MINIMALE_STAP_TIJD_HAND = 15000;  // minimale tijd voordat de stap knop weer ingedrukt mag worden
const unsigned long STAP_TIJD_AUTO = 30000;           // tijd waarna de stap knop automatisch ingedrukt wordt in auto stand

/* ---- Definieren van de state machine en delay ---- */
const int DEFAULT_STATE_DELAY = 20;
StateMachine waterbak = StateMachine();

/* ---- Definieren van de states en toevoegen aan de state machine ---- */
State* WaterNiveauCheck = waterbak.addState(&waterNiveauCheck);
State* WaterNiveauAlarm = waterbak.addState(&waterNiveauAlarm);
State* Zomer = waterbak.addState(&zomer);
State* ZomerRegen = waterbak.addState(&zomerRegen);
State* Winter = waterbak.addState(&winter);
State* WinterRegen = waterbak.addState(&winterRegen);
State* GemaalAan = waterbak.addState(&gemaalAan);
State* LeegLoop = waterbak.addState(&leegLoop);

/* ---- Globale hulp booleans ---- */
bool L_LED_STATE = true;
bool STAP_LED_STATE = true;
bool MINIMALE_STAP_TIJD_VERSTREKEN = false;
bool STAP_KNOP_INGEDRUKT = false;
bool AUTO_STAP_TIJD_VOORBIJ = false;
bool STAP_KNOP_LED_AAN = false;

/* ---- Globale hulp integers ---- */
int gemaalKnipperHulp = 0;  // Teller om een bepaald knipper patroon mogelijk te maken (zie GemaalAan methode)

/* ---- Definitie van timers ---- */
// LET OP, deze timers werken alleen in meervouden van DEFAULT_STATE_DELAY omdat er een blocking delay in loop() zit
Neotimer knipperLangzaamTimer = Neotimer(700);
Neotimer knipperSnelTimer = Neotimer(150);
Neotimer knipperStatusLedTimer = Neotimer(100);
Neotimer minimaleStapTijdHandTimer = Neotimer(MINIMALE_STAP_TIJD_HAND);
Neotimer stapTijdAutoTimer = Neotimer(STAP_TIJD_AUTO);

/* ---- Definitie van het hulp object om de knop te ontdenderen ---- */
Bounce2::Button stapKnop = Bounce2::Button();

/* ---- Deze methode wordt 1 keer na het op starten van de arduino uitgevoerd ----*/
void setup() {
  /* ---- Toevoegen van transities aan de state machine ----*/
  // LETOP: de overgangen naar LeegLoop zijn bewust als laatste gezet zodat de andere overgangen voorrang krijgen.
  WaterNiveauCheck->addTransition(&overgangWaterNiveauCheckWaterNiveauAlarm, WaterNiveauAlarm);
  WaterNiveauCheck->addTransition(&overgangWaterNiveauCheckZomer, Zomer);
  WaterNiveauCheck->addTransition(&overgangWaterNiveauCheckLeegLoop, LeegLoop);

  WaterNiveauAlarm->addTransition(&overgangWaterNiveauAlarmZomer, Zomer);
  WaterNiveauAlarm->addTransition(&overgangWaterNiveauAlarmLeegLoop, LeegLoop);

  Zomer->addTransition(&overgangZomerZomerRegenAuto, ZomerRegen);
  Zomer->addTransition(&overgangZomerZomerRegenStap, ZomerRegen);
  Zomer->addTransition(&overgangZomerLeegLoop, LeegLoop);

  ZomerRegen->addTransition(&overgangZomerRegenWinterAuto, Winter);
  ZomerRegen->addTransition(&overgangZomerRegenWinterStap, Winter);
  ZomerRegen->addTransition(&overgangZomerRegenLeegLoop, LeegLoop);

  Winter->addTransition(&overgangWinterWinterRegenAuto, WinterRegen);
  Winter->addTransition(&overgangWinterWinterRegenStap, WinterRegen);
  Winter->addTransition(&overgangWinterLeegLoop, LeegLoop);

  WinterRegen->addTransition(&overgangWinterRegenGemaalAan, GemaalAan);
  WinterRegen->addTransition(&overgangWinterRegenLeegLoop, LeegLoop);

  GemaalAan->addTransition(&overgangGemaalAanZomer, Zomer);
  GemaalAan->addTransition(&overgangGemaalAanLeegLoop, LeegLoop);

  LeegLoop->addTransition(&overgangLeegLoopWaterNiveauCheck, WaterNiveauCheck);

  /* ---- Het zetten van de juiste modus van de digitale pins (in of output) ----*/
  pinMode(Vlotter, INPUT);
  pinMode(SSHand, INPUT);
  pinMode(SSAuto, INPUT);
  pinMode(WaterNiveauSensor, INPUT);
  pinMode(LedStap, OUTPUT);
  pinMode(K1, OUTPUT);
  pinMode(K2, OUTPUT);
  pinMode(K3, OUTPUT);
  pinMode(K4, OUTPUT);
  pinMode(K6, OUTPUT);
  pinMode(P1, OUTPUT);
  pinMode(P2, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(SB_Actief, INPUT);

  /* ---- Initialiseren van het hulp object voor het ontdenderen van de Stap knop ---- */
  stapKnop.attach(Stap, INPUT);    // hang de ontdenderhulp aan de pin van de Stap knop en intialiseer als input
  stapKnop.setPressedState(HIGH);  // Defineer het hoog zijn van de ingang als het ingedrukt zijn van de knop
  stapKnop.interval(25);           // Reageer voor dit aantal miliseconden niet meer op de ingang (ontdenderen)

  if (SERIAL_DEBUG) {
    Serial.begin(9600);
  }

  if (SOUNDBOARD_AAN) {
    Serial1.begin(9600);
  }
}

/* ---- Deze functie wordt keer op keer uitgevoerd zolang de arduino stroom heeft (main methode) ----*/
void loop() {

  // Code in deze if wordt 1 keer uitgevoerd aan het begin van elke stap van de state machine
  if (waterbak.executeOnce) {
    minimaleStapTijdHandTimer.start();  // start de minimale stap tijd timer voor hand modus
  }

  if (getStandenSchakelaarStand() == HAND) {
    stapKnop.update();  // Update de knop ontdender hulp

    if (minimaleStapTijdHandTimer.done()) {
      STAP_KNOP_LED_AAN = true;
    }

    if (stapKnop.pressed() && STAP_KNOP_LED_AAN) {
      STAP_KNOP_INGEDRUKT = true;
      STAP_KNOP_LED_AAN = false;
    }
  }

  // Als de auto stap timer is afgelopen zet de hulp boolean op true en herstart de timer
  if (getStandenSchakelaarStand() == AUTO && stapTijdAutoTimer.repeat()) {
    AUTO_STAP_TIJD_VOORBIJ = true;
  }

  // Toggle de status LED als keep-alive. Als de L-LED knippert lopen we nog door de states
  if (knipperStatusLedTimer.repeat()) {
    toggleStatusLed();
  }

  // Executeren van de state machine.
  // Dit zorgt voor het uitvoeren van de code in de stappen en het evalueren van de overgangen
  waterbak.run();
  delay(DEFAULT_STATE_DELAY);  // slaap voor een aantal milliseconden om de cpu te ontlasten

  // Wordt gebruikt voor debugging van de code via de serical console van de Arduino IDE
  if (SERIAL_DEBUG && debugTimer.repeat()) {
    Serial.println("Sensor          | Value");
    Serial.print("Water Niveau        |\t");
    Serial.println(analogRead(WaterNiveauSensor));
    Serial.print("Water Percentage  |\t");
    Serial.println(getWaterNiveauPercentage());
    Serial.print("Vlotter             |\t");
    Serial.println(getVlotterStand());
    Serial.print("Standenschakelaar   |\t");
    Serial.println(getStandenSchakelaarStand());
    Serial.print("Stap knop ingedrukt |\t");
    Serial.println(STAP_KNOP_INGEDRUKT);
    Serial.println("-------------------------------");
  }

  // Reset de stap knop ingedrukt hulp boolean
  STAP_KNOP_INGEDRUKT = false;

  // Reset de auto stap tijd hulp boolean
  AUTO_STAP_TIJD_VOORBIJ = false;
}

/* ---- Deze functies beschrijven de logica in de states die boven gedefinieerd zijn ----*/
void waterNiveauCheck() {
  digitalWrite(K1, HIGH);
  digitalWrite(K2, HIGH);
  digitalWrite(K3, LOW);
  digitalWrite(K4, LOW);
  digitalWrite(K6, HIGH);
  digitalWrite(P1, LOW);
  digitalWrite(P2, LOW);

  digitalWrite(LedStap, LOW);
}

void waterNiveauAlarm() {
  digitalWrite(K1, HIGH);
  digitalWrite(K2, HIGH);
  digitalWrite(K3, LOW);
  digitalWrite(K4, LOW);
  digitalWrite(K6, HIGH);
  digitalWrite(P1, LOW);
  digitalWrite(P2, LOW);

  // Toggle Stap LED elke keer als de timer afloopt
  if (knipperSnelTimer.repeat()) {
    toggleStapLed();
  }
}

void zomer() {
  digitalWrite(K1, LOW);
  digitalWrite(K2, HIGH);
  digitalWrite(K3, LOW);
  digitalWrite(K4, LOW);
  digitalWrite(K6, HIGH);
  digitalWrite(P1, HIGH);
  digitalWrite(P2, LOW);

  if (STAP_KNOP_LED_AAN) {
    digitalWrite(LedStap, HIGH);
  } else {
    digitalWrite(LedStap, LOW);
  }
}

void zomerRegen() {
  digitalWrite(K1, LOW);
  digitalWrite(K2, HIGH);
  digitalWrite(K3, HIGH);
  digitalWrite(K4, LOW);
  digitalWrite(K6, HIGH);
  digitalWrite(P1, HIGH);
  digitalWrite(P2, LOW);

  if (STAP_KNOP_LED_AAN) {
    digitalWrite(LedStap, HIGH);
  } else {
    digitalWrite(LedStap, LOW);
  }
}

void winter() {
  digitalWrite(K1, LOW);
  digitalWrite(K2, LOW);
  digitalWrite(K3, LOW);
  digitalWrite(K4, HIGH);
  digitalWrite(K6, LOW);
  digitalWrite(P1, HIGH);
  digitalWrite(P2, LOW);

  if (STAP_KNOP_LED_AAN) {
    digitalWrite(LedStap, HIGH);
  } else {
    digitalWrite(LedStap, LOW);
  }
}

void winterRegen() {
  digitalWrite(K1, LOW);
  digitalWrite(K2, LOW);
  digitalWrite(K3, HIGH);
  digitalWrite(K4, HIGH);
  digitalWrite(K6, LOW);
  digitalWrite(P1, HIGH);
  digitalWrite(P2, LOW);

  if (STAP_KNOP_LED_AAN) {
    digitalWrite(LedStap, HIGH);
  } else {
    digitalWrite(LedStap, LOW);
  }
}

void gemaalAan() {
  digitalWrite(K1, HIGH);
  digitalWrite(K2, HIGH);
  digitalWrite(K3, LOW);
  digitalWrite(K4, LOW);
  digitalWrite(K6, LOW);
  digitalWrite(P1, LOW);
  digitalWrite(P2, HIGH);

  if (SOUNDBOARD_AAN && digitalRead(SB_Actief == LOW)) {
    soundboard.playTrack(8);
  }

  // // Toggle Stap LED in een knipper, knipper, pauze patroon
  // if (knipperSnelTimer.repeat()) {
  //   gemaalKnipperHulp++;
  //   if (gemaalKnipperHulp <= 4) {
  //     toggleStapLed();
  //   }

  //   if (gemaalKnipperHulp >= 13) {
  //     gemaalKnipperHulp = 0;
  //   }
  // }

  minimaleStapTijdHandTimer.reset();
}

void leegLoop() {
  digitalWrite(K1, HIGH);
  digitalWrite(K2, HIGH);
  digitalWrite(K3, LOW);
  digitalWrite(K4, LOW);
  digitalWrite(K6, HIGH);
  digitalWrite(P1, LOW);
  digitalWrite(P2, LOW);

  // Toggle Stap LED elke keer als de timer afloopt
  if (knipperLangzaamTimer.repeat()) {
    toggleStapLed();
  }

  // reset de timers zodat ze weer van voor af aan beginnen als hand of auto wordt gekozen
  minimaleStapTijdHandTimer.reset();
  stapTijdAutoTimer.reset();
}

/* ---- Deze functies definieren de logica in de transities die boven gedefinieerd zijn ----*/
/* ---- (een transitie start als de methode TRUE retouneert)                            ----*/
bool overgangWaterNiveauCheckLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangWaterNiveauCheckWaterNiveauAlarm() {
  return getVlotterStand() == TELAAG;
}

bool overgangWaterNiveauCheckZomer() {
  return getVlotterStand() == OK && (getStandenSchakelaarStand() == HAND || getStandenSchakelaarStand() == AUTO);
}

bool overgangWaterNiveauAlarmLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangWaterNiveauAlarmZomer() {
  return getVlotterStand() == OK && (getStandenSchakelaarStand() == HAND || getStandenSchakelaarStand() == AUTO);
}

bool overgangZomerLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangZomerZomerRegenAuto() {
  return getStandenSchakelaarStand() == AUTO && AUTO_STAP_TIJD_VOORBIJ;
}

bool overgangZomerZomerRegenStap() {
  return getStandenSchakelaarStand() == HAND && STAP_KNOP_INGEDRUKT;
}

bool overgangZomerRegenLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangZomerRegenWinterAuto() {
  return getStandenSchakelaarStand() == AUTO && AUTO_STAP_TIJD_VOORBIJ;
}

bool overgangZomerRegenWinterStap() {
  return getStandenSchakelaarStand() == HAND && STAP_KNOP_INGEDRUKT;
}

bool overgangWinterLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangWinterWinterRegenAuto() {
  return getStandenSchakelaarStand() == AUTO && AUTO_STAP_TIJD_VOORBIJ;
}

bool overgangWinterWinterRegenStap() {
  return getStandenSchakelaarStand() == HAND && STAP_KNOP_INGEDRUKT;
}

bool overgangWinterRegenLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangWinterRegenGemaalAan() {
  return (getWaterNiveauPercentage() >= GEMAAL_AAN_PERCENTAGE && ((getStandenSchakelaarStand() == HAND && STAP_KNOP_LED_AAN) || (getStandenSchakelaarStand() == AUTO && AUTO_STAP_TIJD_VOORBIJ)));
  //return getWaterNiveauPercentage() >= GEMAAL_AAN_PERCENTAGE;
}

bool overgangGemaalAanLeegLoop() {
  return getStandenSchakelaarStand() == NUL;
}

bool overgangGemaalAanZomer() {
  return getWaterNiveauPercentage() <= GEMAAL_UIT_PERCENTAGE;
}

bool overgangLeegLoopWaterNiveauCheck() {
  return getStandenSchakelaarStand() == HAND || getStandenSchakelaarStand() == AUTO;
}

/* ---- Hulp functies ----*/
//Toggle de status van de L LED (onboard)
void toggleStatusLed() {
  digitalWrite(LED_BUILTIN, (L_LED_STATE ? HIGH : LOW));
  L_LED_STATE = !L_LED_STATE;
}

//Toggle de status van de Stap LED
void toggleStapLed() {
  digitalWrite(LedStap, (STAP_LED_STATE ? HIGH : LOW));
  STAP_LED_STATE = !STAP_LED_STATE;
}

// Haal de stand op van de standenschakelaar welke meerdere inputs gebruikt.
StandenSchakelaarStand getStandenSchakelaarStand() {
  StandenSchakelaarStand schakelaarStand = NUL;

  if (digitalRead(SSHand) == HIGH) {
    schakelaarStand = HAND;
  }

  if (digitalRead(SSAuto) == HIGH) {
    schakelaarStand = AUTO;
  }

  return schakelaarStand;
}

// Haald de stand van de vlotter op
VlotterStand getVlotterStand() {
  VlotterStand vlotterStand = TELAAG;

  // Vlotter is altijd OK als debug stand is geactiveerd
  if (digitalRead(Vlotter) == HIGH || SERIAL_DEBUG) {
    vlotterStand = OK;
  }

  return vlotterStand;
}

// Haald de waterstand in de wetering op, vertaald van een waarde op de sensor naar een percentage
int getWaterNiveauPercentage() {
  return map(analogRead(WaterNiveauSensor), WATER_SENSOR_MIN, WATER_SENSOR_MAX, 0, 100);
}