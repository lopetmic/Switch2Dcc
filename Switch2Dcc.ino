/* ##################### Stellpult - Zentrale  ##############################

   Aufgrund der angeschlossenen Weichenschalter wird ein Dcc-Signal erzeugt,
   um entsprechende Weichendecoder ansteuern zu können.
   Die Weichensahalter sind diodenentkoppelte Ein/Aus Schalter, die in einer Matrix angeordnet werden.
   Die MAtrix kann durch Wahl der Pins frei konfiguriert werden.
   Das DCC-Signal wird per Timer2 erzeugt mit fast PWM, Prescaler 32. Pro Bit ist ein Irq notwendig.
   Der Irq wird per OCRB in der Mitte eines Bits ausgelöst und setzt die Timerwerte für das nächste Bit.

*/

// ########################### Definitionen ##################################

const uint8_t U8_Version_Switch2DCC = 4;   // basiert auf Switch2DCC Veriosn 3 von Franz-Peter Müller

/* Schaltermatrix
   Schaltermatrix - für die Matrix können beliebige Ports verwendet werden
   - arU8_WeicheColsP: Matrix-Input (Col) - Die verwendeten Ports werden in weicheColsP festgelegt
   - arU8_WeicheRowsP: Matrix Output(Row) - Die verwendeten Ports werden in weicheRowsP festgelegt
   - arU8_WeicheAddr: Adressen der zu schaltenden Weichen und Row/Column des zugehörigen Schalters (Matrix)
    Schalter können durch mehrfache Angabe der Reihe/Spalte Telegramme für mehrere Adressen erzeugen.
    Dies ist hilfreich, wenn Aktionen in mehreren Decodern oder bei mehreren Zubehörteilen nötig sind.
    Wird die Adresse 0 eingetragem, so kennzeichnet dies einen Reserve- oder logischen Schalter.
    Für diese wird kein DCC Telegramm generiert.
   - arU8_WeicheDir:
    Bit 0 bestimmt die Richtung in der geschaltet wird (bzw welche Spule eines Doppelspulenantriebs betromt wird)
    Bit 1 bestimmt, ob auch Abschaltbefehle gesendet werden -> beide Spulen einer Adresse inaktiv
   - arI16_BlockAddr: Hiermit können Verriegelungen auf Adressbasis konfiguriert werden.
    Positive Zahlen Verriegeln die angegebene Adresse
    Negative Zahlen Geben die angegebene Adresse frei
    - Umsetzung der Adresseb auf Indizes zum einfacheren Zugriff
*/
/*                                    S1          S2      S3  S4  S5      S6   S7  S8  S9 S10                     S11 S12 S13 S14 S15             S16 S17     S18 S19 S20                     S21 S22 S23 S24 S25     */
const uint8_t arU8_WeicheColsP[]   = { 4,  4,  4,  4,  4,  4,  4,  4,  4,  5,   5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8 };
const uint8_t arU8_WeicheRowsP[]   = { 9,  9,  9, 10, 10, 11, 12, 14, 15,  9,  10, 11, 12, 14, 15, 14, 15, 14, 15,  9, 10, 11, 12, 14, 15, 14, 15,  9, 10, 10, 11, 12, 14, 15, 14, 15, 14, 15,  9, 10, 11, 12, 14, 15 };
const uint8_t arU8_WeicheAddr[]    = { 1,  9, 17,  0,  0,  3,  4,  5,  5,  7,  10, 18,  0,  6,  6, 11, 11, 19, 19, 26, 25,  0,  0, 12, 12, 20, 20,  2, 14, 28, 22,  0,  8,  8, 13, 13, 21, 21,  0,  0,  0,  0, 27, 27 };
const uint8_t arU8_WeicheDir[]     = { 1,  1,  1,  1,  1,  1,  1,  7,  3,  1,   1,  1,  1,  7,  3,  7,  3,  7,  3,  0,  0,  1,  1,  7,  3,  7,  3,  1,  1,  1,  1,  1,  7,  3,  7,  3,  7,  3,  1,  1,  1,  1,  3,  7 };
#define DIRMSK  1
#define OFFMSK  2
#define COILMSK 4
const int16_t arI16_BlockAddr[]    = { 0,  0,  0, -3, -4, -4,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 };
uint8_t arU8_BlockIdx[sizeof(arI16_BlockAddr) / 2];

/* Doku für Schwarzenberg:
   Nr: R/S  Fkt
   S1: 4/9  2L/3L je Modul vorhanden -> auf 3 Modulen!
   S2: 4/10 Entsperren Regelspur -> kein Telegramm, nur Logik!
   S3: 4/11 Weiche Regelspur
   S4: 4/12 Gleissperre Regelspur
   S5: 4/14 Fahrstrom Feldbahn Werkstatt (Spule 1)
   S5: 4/15 Fahrstrom Feldbahn Werkstatt (Spule 2)
   S6: 5/9  Weiche Feldbahn Modul 1
   S7: 5/10 Weiche Feldbahn Modul 2
   S8: 5/11 Weiche Feldbahn Modul 3
   S9: 5/12 -
   S10:5/14 Fahrstrom Feldbahn Strecke hinten (Spule 1) -> auf 3 Modulen!
   S10:5/15 Fahrstrom Feldbahn Strecke hinten (Spule 2) -> auf 3 Modulen!
   S11:6/9  Weiche Feldbahn SBF 1
   S12:6/10 Weiche Feldbahn SBF 2
   S13:6/11 -
   S14:6/12 -
   S15:6/14 Fahrstrom Feldbahn Strecke Mitte (Spule 1) -> auf 2 Modulen!
   S15:6/15 Fahrstrom Feldbahn Strecke Mitte (Spule 2) -> auf 2 Modulen!
   S16:7/9  Licht 1
   S17:7/10 Licht 2
   S18:7/11 Licht 3
   S19:7/12 -
   S20:7/14 Fahrstrom Feldbahn Strecke vorne (Spule 1) -> auf 3 Modulen!
   S20:7/15 Fahrstrom Feldbahn Strecke vorne (Spule 2) -> auf 3 Modulen!
   S21:8/9  -
   S22:8/10 -
   S23:8/11 -
   S24:8/12 -
   S25:8/14 Fahrstrom Feldbahn SBF (Spule 1)
   S25:8/15 Fahrstrom Feldbahn SBF (Spule 2)

   Dec Adr Fkt
     1 1  2L/3L
     1 2  Licht 1
     1 3  Weiche Regelspur
     1 4  Gleissperre Regelspur
     1 5  Fahrstrom Feldbahn Werkstatt
     1 6  Fahrstrom Feldbahn Strecke hinten
     1 7  Weiche Feldbahn Modul 1
     1 8  Fahrstrom Feldbahn Strecke vorne
     2 9  2L/3L
     2 10 Weiche Feldbahn Modul 2
     2 11 Fahrstrom Feldbahn Strecke hinten
     2 12 Fahrstrom Feldbahn Strecke Mitte
     2 13 Fahrstrom Feldbahn Strecke vorne
     2 14 Licht 2
     2 15 frei
     2 16 frei
     3 17 2L/3L
     3 18 Weiche Feldbahn Modul 3
     3 19 Fahrstrom Feldbahn Strecke hinten
     3 20 Fahrstrom Feldbahn Strecke Mitte
     3 21 Fahrstrom Feldbahn Strecke vorne
     3 22 Licht 3
     3 23 frei
     3 24 frei
     4 25 Weiche Feldbahn SBF 1
     4 26 Weiche Feldbahn SBF 2
     4 27 Fahrstrom Feldbahn SBF
     4 28 Licht 2
     4 29 frei
     4 30 frei
     4 31 frei
     4 32 frei
*/

/* Weichenstatus
    für jede Weiche wird der Status in einem Byte hinterlegt
    Bit0: Weichenposition
    Bit1: Flag 'Telegram erzeugen'
    Bit2: Flag 'Weiche gesperrt'
    Bit3: Flag 'Abschaltbefehl senden'; notwendig für Ansteuerung Feldbahn Motorrelais, das sonst immer eine Spule bestromt wird
*/
uint8_t       arU8_WeicheState[ sizeof(arU8_WeicheAddr) ];
#define POSMSK  1
#define TELMSK  2
#define BLKMSK  4

// Variablen für die Ausgabe der DCC-Pakete
uint8_t U8_DCC_State;           // Status der Ausgabe
#define DCC_PREAMBLE  0         // gibt nur 1-Bits aus
#define DCC_DATA      1         // aktuelles Paket ausgeben
#define DCC_IDLE      2         // Idle-Telegramm wird ausgegeben

// Variablen zum Datenpufferhandling
// Für die Datentelegramme ( Weichentelegramme ) gibt es einen Wechselpuffer. Während ein Puffer in
// der ISR ausgegeben wird, kann der andere gefüllt werden
#define DCC_BUF_SIZE 2
uint8_t arU8_DCC_Buf[DCC_BUF_SIZE][10];    // Datenpuffer, incl Prüfsumme
uint8_t arU8_DCC_BufState[DCC_BUF_SIZE];   // aktueller Pufferstatus
#define BUF_IDLE  0             // Puffer inaktiv
#define BUF_WRITE 1             // Puffer wird gerade mit Daten beschrieben
#define BUF_WAIT  2             // Daten gültig, warten auf Ausgabe
#define BUF_OUT   3             // Puffer wird auf DCC-Ausgang geschrieben
#define BUF_SENT  4             // Puffer wurde komplett auf dcc ausgegeben
uint8_t arU8_DCC_BufDataLen[DCC_BUF_SIZE]; // atuelle Datenlänge (incl. Prüfsumme)
uint8_t arU8_DCC_BufDataRep[DCC_BUF_SIZE]; // Wiederholungen bei der Ausgabe
#define MAX_REPEATS 2
//#define ROCOADDR 1             // auskommentieren für Standard NMRA Adressing

// Variablen für die ISR-Routine zur DCC-Ausgabe
#define PREAMBLE_BIT_MIN 16
uint8_t U8_PreambleBitCnt;
uint8_t arU8_DCC_PacketIdle[] = { 0xff, 0x00, 0xff };
// Idle packet. Wird immer ausgegeben, wenn am Ende der Preamble kein Ausgabepuffer bereit steht.

// für debugging ------------------------------------------------------------
// Debug-Ports
#define debug
#include "debugports.cpp"
// #define debug_block

//###################### Ende der Definitionen ##############################
//###########################################################################

//###################### SetUp Arduino         ##############################
void setup() {
#ifdef debug
  Serial.begin( 115200 );
#endif

  DebugPrint( "\n\r---------- Begin of setup ----------\n\r");

  // setup of arU8_BlockIdx[]
  DebugPrint( "\n\rBlock Indizes:             ");
  for ( uint8_t i = 0; i < ( sizeof(arI16_BlockAddr) / 2 ); i++ ) {
    arU8_BlockIdx[i] = 0;
    if (arI16_BlockAddr[i] != 0) {
      for ( uint8_t j = 0; j < sizeof(arU8_WeicheAddr); j++ ) {
        if (abs(arI16_BlockAddr[i]) == arU8_WeicheAddr[j]) {
          arU8_BlockIdx[i] = j;
        }
      }
    }
    DebugPrint( "%d, ", arU8_BlockIdx[i] );
    // init des Weichenstatus auf 0
    arU8_WeicheState[i] = 0;
  }

  // Ports für das Einlesen der Schalter initiieren ( Matrix )

  // in arU8_WeicheRowsP stehen die Outputports der Matrix
  for ( uint8_t i = 0; i < sizeof(arU8_WeicheRowsP); i++ ) {
    pinMode( arU8_WeicheRowsP[i], OUTPUT );
    digitalWrite(  arU8_WeicheRowsP[i], HIGH );
  }

  // in arU8_WeicheColsP stehen die Eingänge der Matrix mit Pullup
  for ( uint8_t i = 0; i < sizeof(arU8_WeicheColsP); i++ ) {
    pinMode( arU8_WeicheColsP[i], INPUT_PULLUP );
  }

  // Schalter einlesen und alle auf 'geaendert' stellen, so dass beim Programmstart
  // die entsprechenden Weichenbefehle ausgegeben werden.
  DebugPrint( "\n\rWeichen Status roh:        ");
  for ( uint8_t i = 0; i < sizeof(arU8_WeicheState); i++ ) {
    arU8_WeicheState[i] |= (mtGetSwitch(i) & POSMSK );
    // Telegrammbit setzen, wenn Adresse es erfordert
    if (arU8_WeicheAddr[i] != 0 ) {
      arU8_WeicheState[i] |= TELMSK;
    }
    DebugPrint( "%d, ", arU8_WeicheState[i] );
  }

  // Verriegelungen in arU8_WeicheState setzen. Dies darf erst geschehen,
  // nachdem der Initialzustand einmal komplett festgestellt wurde
  DebugPrint( "\n\rWeichen Status verriegelt: ");
  for ( uint8_t i = 0; i < sizeof(arU8_WeicheState); i++ ) {
    mtSetBlock( i, arU8_WeicheState[i] & POSMSK );
    DebugPrint( "%d, ", arU8_WeicheState[i] );
  }

  pinMode( 13, OUTPUT );
  digitalWrite(  13, LOW );
  InitTimer2();

  MODE_TP1;
  MODE_TP2;
  MODE_TP4;
  MODE_TP3;

  delay (2000);

  DebugPrint( "\n\r----- End of setup -----\n\r");

}
//###################### Ende SetUp Arduino    ##############################

//###################### Main Loop Arduino     ##############################
void loop() {

  // Alle Schalter lesen und Änderungen markieren
  for ( uint8_t i = 0; i < sizeof( arU8_WeicheState ); i++ ) {
    uint8_t U8_SwitchPsn;   // aktuelle Postition eines Schalters;
    // einlesen ohne Berücksichtigen der Verriegelungen
    U8_SwitchPsn = mtGetSwitch(i);
    // ggfs Verriegelungen updaten, falls der Schalter eine Verriegelung steuert
    mtSetBlock( i, U8_SwitchPsn );
    // wenn Schalterstellung != Weichenstatus, dann
    if ( U8_SwitchPsn != (arU8_WeicheState[i] & POSMSK) ) {
      // neuen Status setzen
      arU8_WeicheState[i] = ( arU8_WeicheState[i] & 0xFE ) + ( U8_SwitchPsn & POSMSK );
      // Prüfen ob Weiche nicht geblockt
      if ( (arU8_WeicheState[i] & BLKMSK) == false ) {
        // Telegrammbit setzen, wenn Adresse es erfordert
        if (arU8_WeicheAddr[i] != 0 ) {
          arU8_WeicheState[i] |= TELMSK;
        }
      }
      DBprintStatus(i);
    }
  }
  //DebugPrint( "----- reading finished -----\n\r");

  // für geänderte Weichen jeweils ein Telegramm erzeugen
  for (uint8_t i = 0; i < sizeof( arU8_WeicheState ); i++ ) {
    uint8_t U8_WeicheCoil;  // anzusteuernde Spule (entspricht der Weichenposition)
    uint8_t U8_WeicheAct;   // aktuelle Aktivität einer Weichenspule (angesteuert oder nicht);
    // Wenn Telegrammbit in arU8_WeicheState gesetzt, dann
    if ( (arU8_WeicheState[i] & TELMSK) != 0 ) {
      // aus Schalterposition und Dir Konfiguration die Parameter für das Protokoll bestimmen:
      if ( ( arU8_WeicheDir[i] & OFFMSK ) > 0 ) {
        // es werden getrennte Spulen auf einer Adresse angesteuert
        // Spulennummer entspricht der DIR Maske
        U8_WeicheCoil = ( arU8_WeicheDir[i] & COILMSK ) >> (COILMSK / 2);
        // Aktivitätslevel aus Weichenposition bestimmen
        // imkl. Rückrechnen einer möglichgen Schalterinvertierung
        U8_WeicheAct = ( arU8_WeicheState[i] & POSMSK );
      } else {
        // es wird eine Spule oder Servo angesteuert
        U8_WeicheCoil = arU8_WeicheState[i] & POSMSK;
        U8_WeicheAct = 1;
      }
      DebugPrint( "Creating DCC Telegram for Index: %d, Adresse: %d, Pos: %d, Act: %d --> ", i, arU8_WeicheAddr[i], U8_WeicheCoil, U8_WeicheAct );
      // Weichentelegramm erzeugen und wenn erfolgreich, dann
      if ( mtCreateTelegram( arU8_WeicheAddr[i], U8_WeicheCoil, U8_WeicheAct ) >= 0 ) {
        // Telegrammbit löschen
        arU8_WeicheState[i] &= ~( TELMSK );
      }
    }
  }
  //DebugPrint( "----------\n\r");

  // Telegramm Wiederholungen verwalten
  // Für alle Puffer
  for (uint8_t i = 0; i <= DCC_BUF_SIZE; i++ ) {
    // wenn der Puffer bereits gesendet wurde, dann
    if ( arU8_DCC_BufState[i] == BUF_SENT ) {
      // Puffer wiederholen oder freigeben
      DebugPrint( "Buffer[%d] gesendet, RepCnt=%d\n\r", i , arU8_DCC_BufDataRep[i] );
      if ( arU8_DCC_BufDataRep[i] > 1 ) {
        // Telegramm erneut senden
        arU8_DCC_BufDataRep[i]--;
        arU8_DCC_BufState[i] = BUF_WAIT;
      } else {
        arU8_DCC_BufState[i] = BUF_IDLE;
      }
    }
  }
  //DebugPrint( "----------\n\r");

  delay(1);
}
//###################### Ende Loop Arduino     ##############################
//###########################################################################

/* ##################### mtGetSwitch           ##############################
  liest die Position eines Schalters der Matrix und liefert diesen zurück
  Input:  Index des Schalters, der gelesen werden soll
  Output: Position der gelesenen Weiche
*/
uint8_t mtGetSwitch(uint8_t U8_WeicheIdx) {
  digitalWrite( arU8_WeicheRowsP[U8_WeicheIdx], LOW );
  uint8_t U8_SwitchPsn = digitalRead( arU8_WeicheColsP[U8_WeicheIdx] );
  digitalWrite( arU8_WeicheRowsP[U8_WeicheIdx], HIGH );
  U8_SwitchPsn = U8_SwitchPsn ^ ( arU8_WeicheDir[U8_WeicheIdx] & DIRMSK );     // Schalterorientierung
  return U8_SwitchPsn;
}

/* ##################### mtSetBlock            ##############################
  prueft die Verriegelungskonfiguration eines Schalters und
  aendert den Verriegelungsstatus einer Weiche aufgrund des Schalters
  Input 1:  Index des Schalters, der geprüft werden soll
  Input 2: Position der Schalters
*/
void mtSetBlock(uint8_t U8_BlockSwIdx, uint8_t BlockSwPsn) {
  // Verriegelungen nur bearbeiten, wenn der Schalter selbst freigegeben ist
  if ( ( arU8_WeicheState[U8_BlockSwIdx] & BLKMSK ) == 0 ) {
    if (arI16_BlockAddr[U8_BlockSwIdx] > 0) { // es gibt eine Verriegelungsbeziehung
      if (BlockSwPsn > 0) {      // Verriegeln
        arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]] |= BLKMSK;
        arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]] &= (~TELMSK);
        #ifdef debug_block
        DebugPrint( "\n\rBlock detected due to switch %d: blocked %d, state %d \n\r", U8_BlockSwIdx, arU8_BlockIdx[U8_BlockSwIdx], arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]]);
        #endif
      } else {                    // Freigeben
        arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]] &= (~BLKMSK);
        #ifdef debug_block
        DebugPrint( "\n\rRelease detected due to switch %d: released %d, state %d \n\r", U8_BlockSwIdx, arU8_BlockIdx[U8_BlockSwIdx], arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]]);
        #endif
      }
    } else {
      if (arI16_BlockAddr[U8_BlockSwIdx] < 0) { // es existiert eine Freigabebeziehung
        if (BlockSwPsn == 0) {      // Verriegeln
          arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]] |= BLKMSK;
          arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]] &= (~TELMSK);
          #ifdef debug_block
          DebugPrint( "\n\rBlock detected due to switch %d: blocked %d, state %d \n\r", U8_BlockSwIdx, arU8_BlockIdx[U8_BlockSwIdx], arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]]);
          #endif
        } else {                    // Freigeben
          arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]] &= (~BLKMSK);
          #ifdef debug_block
          DebugPrint( "\n\rRelease detected due to switch %d: released %d, state %d \n\r", U8_BlockSwIdx, arU8_BlockIdx[U8_BlockSwIdx], arU8_WeicheState[arU8_BlockIdx[U8_BlockSwIdx]]);
          #endif
        }
      }
    }
  }
}
/* ###################### DCC Zubehörbefehle   ##############################

  siehe auch: http://www.opendcc.de/info/dcc/dcc.html

  Zubehör kann mit zwei verschiedenen Befehlen angesprochen werden,
  dem 'einfachen' (=basic) und das 'erweiterte' (=extended) Format.

  hier wird das einfache Format verwendet und beschrieben:

  Das einfache Format benutzt zwei Bytes zur Kodierung:
   Format  10AAAAAA 0 1AAADAAC 0 [XOR]
   AAAAAAAA  bezeichnet die Adresse, hier haben sich aufgrund der historischen Entwicklung bestimmte Besonderheiten ergeben
   D kennzeichnet den 'Aktivierungszustand', also ob der Ausgang ein- oder ausgeschaltet wird.
   C kennzeichnet die Spule. (Coil)
  Der Befehl lehnt sich eng an die ersten 4-fach Weichenantriebe mit paarweisen Spulen an.
  Deshalb ist die Auswahl des Spulenpaares in den Bits 2 und 1 des zweiten Bytes.
  Welche Spule aktiviert wird, legt das Bit C fest, somit bilden die letzten drei Bits (AAC) die eigentliche Ausgangsadresse.
  C = 0 soll dabei Weiche auf Abzweig bzw. Signal auf Halt kennzeichnen.
  D = 1 bedeutet Spule (oder Ausgang) einschalten, D = 0 bedeutet ausschalten.

  Beispiel:
   Preamble       Startbit  Adressbyte  Startbit  Datenbyte  Startbit  Prüfsumme  Stopbit
   11111111111111 0         10AAAAAA    0         1AAA1BBR   0         [xor]      1

   Dabei bedeutet im Adressbyte AAAAAA [A7 .. A2] und im Datenbyte AAA [A10 .. A8], wobei die Adressen A10 bis A8 invertiert übertragen werden.
   BB [A1 .. A0] ist die lokale Adresse am Decoder (0,1,2,3).
   R ist das Outputbit, d.h. welche Spule aktiviert werden soll.

  Aus den übermittelten Adressen wird wie folgt das DCC-Telegramm zusammengebaut:
   Adressbyte = 0x80 + ((adresse >> 2) & 0x3F);
   Datenbyte  = 0x80 + (adresse >> 8) ^ 0x07) * 0x10;
   Prüfsumme  = Adressbyte ^ Datenbyte;

  Hinweise:
   Die Adressbits sind über den Befehl verteilt, in der CV-Übersicht gibt es eine Grafik, welche den Zusammenhang erläutert (http://www.opendcc.de/info/decoder/dcc_cv.html)
   Man beachte die Invertierung der mittleren Bits.
   Die Dekoderadresse 0 soll nicht benutzt werden, die erste zu benutzende Adresse ist also die Adresse 4: 10000001 0 1111D00C 0 [XOR].
   Diese soll von Bediengeräten als '1' dargestellt werden.

  Notaus
   Ein Zubehörbefehl mit allen Adressbit = 1 (Spulenadresse jedoch 0, Spule deaktiviert)
   Also Adresse 2047, Codierung 10111111 0 10000110 0 [XOR] bedeutet Notaus.

  Hier wird das wie folgt abgebildet:
  Die übergebene Adresse wird ab 1 gezählt. In der Ausgabeeinheit wird das auf die obigen Bits abgebildet, also Dekoderadresse, Ausgangspaar und zu aktivierende Spule bestimmt.
*/
/* ##################### mtCreateTelegram      ##############################
  Es wird ein Weichentelegramm für die übergebene Weichennummer erzeugt.
  Input1: Index der Weiche, für die ein Telegramm erzeugt werden soll
  Input2: aktuelle Stellung der Weiche (=anzusteuernde Spule)
  Input3: Aktivitätszustand der auszugebenden Spule
  Output: Funktionswert ist der Puffer oder -1 wenn kein freier Puffer vorhanden ist
*/
int8_t mtCreateTelegram( uint8_t U8_WeicheAddr, uint8_t U8_WeicheCoil, uint8_t U8_WeicheAct ) {

  uint8_t U8_DCC_BufDataIdx;
  uint8_t U8_DCC_BufIdx;
  uint8_t U8_DCC_ChkSum;
  uint16_t U16_DCC_Addr;

  if ( U8_WeicheAddr > 0 ) {
    // Weichenadressen zählen ab 1, die DCC Adressierung ab 0
    // Standardmässig werden die ersten 4 Adressen nicht verwendet, d.h
    // Weichenadresse 1 -> DCC Adresse 4
    // Bei Roco-Addressing: Weichenadresse 1 -> DCC Adresse 0
#ifdef ROCOADDR
    U16_DCC_Addr = U8_WeicheAddr - 1;
#else
    U16_DCC_Addr = U8_WeicheAddr + 3;
#endif

    // freien Puffer suchen
    for ( U8_DCC_BufIdx = 0; U8_DCC_BufIdx < DCC_BUF_SIZE; U8_DCC_BufIdx++ ) {
      if ( arU8_DCC_BufState[U8_DCC_BufIdx] == BUF_IDLE ) break;
    }
    if ( U8_DCC_BufIdx < DCC_BUF_SIZE ) {

      // Buffer Verwaltungsdaten initialisieren
      arU8_DCC_BufState[U8_DCC_BufIdx] = BUF_WRITE;
      arU8_DCC_BufDataRep[U8_DCC_BufIdx] = MAX_REPEATS; // Weichentelegramme MAX_REPEATS x wiederholen
      U8_DCC_BufDataIdx = 0;
      U8_DCC_ChkSum = 0;

      // DCC-Adressbyte bestimmen
      // 1. Byte ist b10xxxxxx , wobei x die Bits 7-2 der Weichenadresse sind
      arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx] = 0x80 + ((U16_DCC_Addr >> 2 ) & 0b00111111 );
      U8_DCC_ChkSum ^= arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx++];

      // DCC-Datenbyte setzt sich zusammen aus der High-Adresse, den untersten 2 Bits der Weichenadresse
      // und der Spulenadresse (Richtung)
      arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx] =  0x80 |
          ((((U16_DCC_Addr >> 8) & 0x07 ) ^ 0x07 ) << 4 ) |
          (( U8_WeicheAct & 0x01 ) << 3 ) |
          (( U16_DCC_Addr & 0x03 ) << 1 ) |
          ( U8_WeicheCoil & 0x01 );
      U8_DCC_ChkSum ^= arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx++];

      // DCC Checksum in Puffer
      arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx++] = U8_DCC_ChkSum;

      // Puffer VErwaltungsdaten aktualisieren
      arU8_DCC_BufDataLen[U8_DCC_BufIdx] = U8_DCC_BufDataIdx;
      arU8_DCC_BufState[U8_DCC_BufIdx] = BUF_WAIT;

      DebugPrint( "DCCBuf[%d]: Weiche=%d, DccAdr=%d, Rep:%d Data: %x:%x:%x\n\r", U8_DCC_BufIdx, U8_WeicheAddr, U16_DCC_Addr, arU8_DCC_BufDataRep[U8_DCC_BufIdx], arU8_DCC_Buf[U8_DCC_BufIdx][0], arU8_DCC_Buf[U8_DCC_BufIdx][1], arU8_DCC_Buf[U8_DCC_BufIdx][2]);
    } else {
      U8_DCC_BufIdx = -1;
      DebugPrint( "kein freierPuffer, abbrechen >>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\r" );
    }
  } else {
    // in der Konfiguration ist eine 0 eingetragen (Reserveschalter oder Logik)
    U8_DCC_BufIdx = -2;
    DebugPrint( "keine Adresse zugewiesen (Leerschalter), abbrechen >>>>>>>\n\r" );
  }

  return U8_DCC_BufIdx;
}

//###################### Timer 2 Handling      ##############################

//###################### Definitionen          ##############################
#define BIT0_LEN  (232 / 2)  // Bitlength in Timer2-Ticks
#define BIT1_LEN  (116 / 2)
#define SET_BIT0 {OCR2A = BIT0_LEN - 1; OCR2B = BIT0_LEN / 2 - 1;}
#define SET_BIT1 {OCR2A = BIT1_LEN - 1; OCR2B = BIT1_LEN / 2 - 1;}

//###################### Timer 2 init           ##############################
void InitTimer2(void) {
  // fast PWM  mit Top = OCR2A;
  // Ausgangssignal Output Compare Register OCR2B ist aktiv
  // Prescaler = 32 (=2µs / Takt)
  SET_BIT1;                 // Ausgabe von 1 Bits für die Preamble
  TCCR2A = (1 << COM2B1 )   // Compare Match Output B Mode COM2B1:0 = 1/0: Clear OC2B on Compare Match, set OC2B at BOTTOM
           | (0 << COM2B0 )
           | (1 << WGM21 )
           | (1 << WGM20 ); // Waveform Generation Mode WGM22:0 = 1/1/1: FastPWM, OCRA=Top
  TCCR2B = (1 << WGM22 )
           | (0 << CS22  )
           | (1 << CS21  )
           | (1 << CS20  ); // Clock Select CS22:0 = 0/1/1 clk/32 from prescaler
  TIMSK2 = (1 << OCIE2B );  // Interrupt on OCRB match

  // DCC-Signal initialisieren
  U8_DCC_State = DCC_PREAMBLE;
  U8_PreambleBitCnt = 0;

  // DDC Output initialisieren
  pinMode( 3, OUTPUT ); // OCR2B gibt auf D3 aus;
}

//###################### Timer 2 interrupt service ##############################
ISR ( TIMER2_COMPB_vect) {
  static uint8_t U8_BitCount;        // Bitzähler (zeigt auf das aktuell ausgegebene Bit )
  static uint8_t U8_ByteCount;       // Bytezähler im aktuellen Paket
  static uint8_t *ptrU8_PacketBuf;   // Zeiger auf das aktuell auszugebende DCC-Paket
  static int8_t U8_DCC_BufSndIdx;    // Puffer der gerade per ISR ausgegeben wird (=< 0, wenn keine Ausgabe aktiv ist)

  uint8_t U8_DCC_BufWait;
  uint8_t U8_DCC_BufIdx;
  // Set OCRA / OCRB for the next Bit
  static const uint8_t arU8_bitMsk[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

  switch ( U8_DCC_State ) {
    case DCC_PREAMBLE :
      // In der Preamble werden nur 1-Bits ausgegeben (keine Änderung von
      // OCRB notwendig. Solange die Mindestzahl noch nicht erreicht ist,
      // wird nicht auf neues Paket geprüft
      SET_TP1;
      if ( U8_PreambleBitCnt++ > PREAMBLE_BIT_MIN ) {
        SET_BIT0; // Startbit ausgeben
        U8_BitCount = 8;
        // prüfen ob neues Telegramm auszugeben ist.
        U8_DCC_BufWait = 0;
        for ( U8_DCC_BufIdx = 0; U8_DCC_BufIdx < DCC_BUF_SIZE; U8_DCC_BufIdx++ ) {
          if ( arU8_DCC_BufState[U8_DCC_BufIdx] == BUF_WAIT ) {
            U8_DCC_BufWait = U8_DCC_BufWait + arU8_bitMsk[U8_DCC_BufIdx];
          }
        }
        if ( U8_DCC_BufWait > 0 ) {
          // es wartet ein Telegramm auf Ausgabe, Datenpointer setzen
          // drauf achten, dass die Puffer gewechselt werden ( wenn beide auf
          // 'WAIT' stehen
          U8_DCC_State = DCC_DATA;
          if ( U8_DCC_BufWait > 2 ) {
            U8_DCC_BufSndIdx = U8_DCC_BufSndIdx == 0 ? 1 : 0;
          } else {
            U8_DCC_BufSndIdx = U8_DCC_BufWait - 1;
          }
          U8_ByteCount = arU8_DCC_BufDataLen[U8_DCC_BufSndIdx];
          ptrU8_PacketBuf = &arU8_DCC_Buf[U8_DCC_BufSndIdx][0];
          arU8_DCC_BufState[U8_DCC_BufSndIdx] = BUF_OUT;
          digitalWrite(  13, LOW );
        } else {
          // kein Telegramm auszugeben, Idle-Telegramm senden
          U8_DCC_State = DCC_IDLE;
          U8_DCC_BufSndIdx = -1;
          ptrU8_PacketBuf = arU8_DCC_PacketIdle;
          U8_ByteCount = sizeof(arU8_DCC_PacketIdle);
          digitalWrite(  13, HIGH );
        }
      }
      CLR_TP1;
      break;
    case DCC_DATA:
    case DCC_IDLE:
      // Datenbits ausgeben
      if ( U8_BitCount == 0 ) {
        SET_TP2;
        // Byte ist komplett ausgegeben, Stopbit ausgeben
        if ( U8_ByteCount == 1) {
          SET_TP3;
          // war letztes Byte, 1 ausgeben und auf Preamble schalten
          SET_BIT1;
          if ( U8_DCC_State == DCC_DATA ) arU8_DCC_BufState[U8_DCC_BufSndIdx] = BUF_SENT;
          U8_DCC_State = DCC_PREAMBLE;
          U8_PreambleBitCnt = 0;
          CLR_TP3;
        } else {
          // Stopbit ausgeben und auf nächstes Byte schalten
          SET_BIT0;
          U8_BitCount = 8;
          ptrU8_PacketBuf++;
          U8_ByteCount--;
        }
        CLR_TP2;
      } else {
        SET_TP4;
        // Datenbit ausgeben
        if ( *ptrU8_PacketBuf & arU8_bitMsk[--U8_BitCount] ) {
          SET_BIT1;
        } else {
          SET_BIT0;
        }
        CLR_TP4;
      }
  }

}


/* ##################### DBprintStatus         ##############################
  gibt den aktuellen Status des Stellpults auf der Debugschnittstelle aus
  Input:  Index des Schalters, der geändert wurde
  Output: keiner
*/

#ifdef debug
void DBprintStatus(uint8_t ChgIdx) {
    DebugPrint( "--------- Debug-Ausgabe Stati ---------\n\r");
    DebugPrint( "\n\r");
    DebugPrint( "S Nr | Addr |  Stat  |  HW  | BLST | xxxx | xxxx | xxxx \n\r");
    for ( uint8_t i = 0; i < sizeof(arU8_WeicheState); i++ ) {
        if ( (arU8_WeicheAddr[i] != 0) || (arU8_BlockIdx[i] != 0) ) {
            DebugPrint( "%4d |%5d | %2d -%2d | %4s | %4s | %4d | %4d | %3s\n\r" , i,
                                                                 arU8_WeicheAddr[i],
                                                                 arU8_BlockIdx[i], arU8_WeicheState[i] ,
                                                                 ( arU8_WeicheState[i] & POSMSK ) == 0 ? "off":" on",
                                                                 ( arU8_WeicheState[i] & BLKMSK ) == 0 ? "":"blkd",
                                                                 0,
                                                                 0,
                                                                 i == ChgIdx ? "<--":"");
            
        }
    }    
}
#else
void DBprintStatus(void) {
    
}
#endif
