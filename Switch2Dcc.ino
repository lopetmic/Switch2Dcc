//#include <MobaTools.h>

/* ##################### Stellpult - Zentrale  ##############################
  
   Aufgrund der angeschlossenen Weichenschalter wird ein Dcc-Signal erzeugt
   um entsprechende Weichendecoder ansteuern zu können.
   Das DCC-Signal wird per Timer2 erzeugt mit fast PWM, Prescaler 32. Pro Bit ist ein Irq notwendig.
   Der Irq wird per OCRB in der Mitte eines Bits ausgelöst und setzt die Timerwerte für das nächste Bit.
   
*/

// ########################### Definitionen ##################################

#define WEICHEN_VERSION_ID 02

// Schaltermatrix
// Bitcodierte Schalter ( max 4x4 oder 4x8 = 16/32 Schalter )
// je nachdem ob die _Schalterbits in einem int(16Bit) oder long(32Bit) gespeichert werden
// Matrix-Input (Col): Pin 4-7 
// Matrix Output(Row): Pin 8-11
// Adressen der zu schaltenden Weichen und Row/Column des zugehörinen Schalters (Matrix)
const uint8_t arU8_WeicheAddr[]    = { 1, 2, 3, 4,   5, 6, 7, 8,   9, 10, 11, 12,  13, 14, 15, 16 };
const uint8_t arU8_WeicheColsP[]   = { 4, 5, 6, 7,   4, 5, 6, 5,   4,  5,  6,  7,   4,  5,  6,  7 };
const uint8_t arU8_WeicheRowsP[]   = { 8, 8, 8, 8,   9, 9, 9, 9,  10, 10, 10, 10,  11, 11, 11, 11 };

// Weichenstatus
// für jede Weiche wird der Status in einem Byte hinterlegt
// Bit0: Weichenposition, Bit1: Flag 'Telegram erzeugen'
uint8_t       arU8_WeicheState[ sizeof(arU8_WeicheAddr) ]; 
#define POSMSK  1
#define TELMSK  2

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
// uint8_t U8_DCC_BufWrtIdx;    // Schreibindex ( es kann immer nur in einen Puffer geschrieben werden )
int8_t U8_DCC_BufSndIdx;        // Puffer der gerade per ISR ausgegeben wird (=< 0, wenn keine Ausgabe aktiv ist)
uint8_t arU8_DCC_BufState[DCC_BUF_SIZE];   // aktueller Pufferstatus
#define BUF_IDLE  0             // Puffer inaktiv
#define BUF_WRITE 1             // Puffer wird gerade mit Daten beschrieben
#define BUF_WAIT  2             // Daten gültig, warten auf Ausgabe
#define BUF_OUT   3             // Puffer wird auf DCC-Ausgang geschrieben
#define BUF_SENT  4             // Puffer wurde komplett auf dcc ausgegeben
uint8_t arU8_DCC_BufDataLen[DCC_BUF_SIZE]; // atuelle Datenlänge (incl. Prüfsumme)
uint8_t arU8_DCC_BufDataRep[DCC_BUF_SIZE]; // Wiederholungen bei der ausgabe
#define MAX_REPEATS 2

// Variablen für die ISR-Routine zur DCC-Ausgabe
#define PREAMBLE_BIT_MIN 14
uint8_t U8_PreambleBitCnt;
uint8_t arU8_DCC_PacketIdle[] = { 0xff, 0x00, 0xff }; 
// Idle packet. Wird immer ausgegeben, wenn am Ende der Preamble kein Ausgabepuffer bereit steht.

// für debugging ------------------------------------------------------------
// Debug-Ports
#define debug
#include "debugports.cpp"

//###################### Ende der Definitionen ##############################
//###########################################################################

//###################### SetUp Arduino         ##############################
void setup() {
  #ifdef debug
  Serial.begin( 115200 );
  #endif
  
  // Ports für das Einlesen der Schalter initiieren ( Matrix )
  DebugPrint( "IniSwitches: " ); 
  
  // in arU8_WeicheRowsP stehen die Outputports der Matrix
  for ( uint8_t i=0; i < sizeof(arU8_WeicheRowsP); i++ ) {
      pinMode( arU8_WeicheRowsP[i], OUTPUT );
      digitalWrite(  arU8_WeicheRowsP[i], HIGH );
  }
  
  // in arU8_WeicheColsP stehen die Eingänge der Matrix mit Pullup
  for ( uint8_t i=0; i < sizeof(arU8_WeicheColsP); i++ ) {
      pinMode( arU8_WeicheColsP[i], INPUT_PULLUP );
  }
  
  // Schalter einlesen und alle auf 'geaendert' stellen, so dass beim Programmstart
  // die entsprechenden Weichenbefehle ausgegeben werden.
  for ( uint8_t i=0; i < sizeof(arU8_WeicheState); i++ ) {
    arU8_WeicheState[i] = TELMSK | mtGetSwitch(i);
    DebugPrint( ",%d", arU8_WeicheState[i] & 1 );
  }
  DebugPrint( "\n\r----------\n\r");
  
  InitTimer2();
  
  MODE_TP1;
  MODE_TP2;
  MODE_TP4;
  MODE_TP3;
}
//###################### Ende SetUp Arduino    ##############################

//###################### Main Loop Arduino     ##############################
void loop() {
  uint8_t i;              // Schleifenzähler
  uint8_t U8_WeichePsn;   // aktuelel Postition einer Weiche;
    
  // Alle Schalter lesen und Änderungen markieren
  for ( i=0; i < sizeof( arU8_WeicheState ); i++ ) {
      // einlesen
      U8_WeichePsn = mtGetSwitch(i);
      // wenn Schalterstellung != Weichenstatus, dann
      if ( U8_WeichePsn != (arU8_WeicheState[i] & POSMSK) ) {
          // neuen Status mit Telegrammbit setzen
          arU8_WeicheState[i] = U8_WeichePsn | TELMSK;
          DebugPrint( "Index: %d, Adresse %d, Status: %d\n\r", i, arU8_WeicheAddr[i], arU8_WeicheState[i] );
      }
  }
  DebugPrint( "\n\r----------\n\r");
      
  // für geänderte Weichen jeweils ein Telegramm erzeugen
  for ( i=0; i < sizeof( arU8_WeicheState ); i++ ) {
      // Wenn Telegrammbit in arU8_WeicheState gesetzt, dann
      if ( (arU8_WeicheState[i] & TELMSK) != 0 ) {
          // aktuelle Schalterposition holen
          U8_WeichePsn = (arU8_WeicheState[i] & POSMSK) != 0;
          DebugPrint( "Index: %d, Adresse %d, Status: %d\n\r", i, arU8_WeicheAddr[i], U8_WeichePsn );
          // Weichentelegramm erzeugen und wenn erfolgreich, dann
          if ( mtCreateTelegram( arU8_WeicheAddr[i], U8_WeichePsn ) >= 0 ) {
              // Telegrammbit löschen
              arU8_WeicheState[i] &= ~( TELMSK );
          }
      }
  }
  DebugPrint( "\n\r----------\n\r");
   
  // Telegramm Wiederholungen verwalten
  // Für alle Puffer
  for ( i=0; i <= DCC_BUF_SIZE; i++ ) {
      // wenn der Puffer bereits gesendet wurde, dann
      if ( arU8_DCC_BufState[i] == BUF_SENT ) {
          // Puffer wiederholen oder freigeben
          DebugPrint( "Buffer[%d] gesendet, RepCnt=%d\n\r", i ,arU8_DCC_BufDataRep[i] );
          if ( arU8_DCC_BufDataRep[i] > 1 ) {
              // Telegramm erneut senden
              arU8_DCC_BufDataRep[i]--;
              arU8_DCC_BufState[i] = BUF_WAIT;
          } else {
              arU8_DCC_BufState[i] = BUF_IDLE;
          }
      }
  }
  DebugPrint( "\n\r----------\n\r");
  
  delay(5);
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
    uint8_t U8_WeichePsn =  digitalRead( arU8_WeicheColsP[U8_WeicheIdx]);
    digitalWrite( arU8_WeicheRowsP[U8_WeicheIdx], HIGH );
    return U8_WeichePsn;
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

   Dabei bedeutet im Adressbyte AAAAAA [A5 .. A0] und im Datenbyte AAA [A8 .. A6], wobei die Adressen A8 bis A6 invertiert übertragen werden. 
   BB ist die lokale Adresse am Decoder (0,1,2,3).
   R ist das Outputbit, d.h. welche Spule aktiviert werden soll. 

 Aus den übermittelten Adressen wird wie folgt das DCC-Telegramm zusammengebaut:
   Adressbyte = 0x80 + (adresse & 0x3F);
   Datenbyte  = 0x80 + (adresse / 0x40) ^ 0x07) * 0x10;
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
 Input2: aktuelle Stellung der Weiche
 Output: Funktionswert ist der Puffer oder -1 wenn kein freier Puffer vorhanden ist
*/
int8_t mtCreateTelegram( uint8_t U8_WeicheAddr, uint8_t U8_WeichePsn ) {

    uint8_t U8_DCC_BufDataIdx;
    uint8_t U8_DCC_BufIdx;
    uint8_t U8_DCC_ChkSum;
    uint16_t U16_DCC_Addr;
    
    // Weichenadressen zählen ab 1, die DCC Adressierung ab 0
    // Standardmässig werden die ersten 4 Adressen nicht verwendet, d.h
    // Weichenadresse 1 -> DCC Adresse 4
    U16_DCC_Addr =  U8_WeicheAddr > 0 ? U8_WeicheAddr + 3 : 4 ;

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
        arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx] = 0b10000000 | ( (U16_DCC_Addr >> 2) & 0b00111111);
        U8_DCC_ChkSum ^= arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx++];
    
        // 2. Byte setzt sich zusammen aus der High-Adresse, den untersten 2 Bits der Weichenadresse
        // und der Spulenadresse
        arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx] =  0b10000000 |
                                  ((((U16_DCC_Addr >> 8) & 0x07 ) ^ 0xff) << 4 ) |
                                  ( 1 << 3 ) |
                                  (( U16_DCC_Addr & 0x03 ) << 1 ) |
                                  ( U8_WeichePsn & 0x01 );
        U8_DCC_ChkSum ^= arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx++];
    
        arU8_DCC_Buf[U8_DCC_BufIdx][U8_DCC_BufDataIdx++] = U8_DCC_ChkSum;
        arU8_DCC_BufDataLen[U8_DCC_BufIdx] = U8_DCC_BufDataIdx;
        arU8_DCC_BufState[U8_DCC_BufIdx] = BUF_WAIT;
    
        DebugPrint( "DCCBuf[%d]: Weiche=%d, DccAdr=%d, Rep:%d Data: %x:%x:%x\n\r", U8_DCC_BufIdx, U8_WeicheAddr, U16_DCC_Addr, arU8_DCC_BufDataRep[U8_DCC_BufIdx],arU8_DCC_Buf[U8_DCC_BufIdx][0],arU8_DCC_Buf[U8_DCC_BufIdx][1],arU8_DCC_Buf[U8_DCC_BufIdx][2]);
    } else {
        U8_DCC_BufIdx = -1;
        DebugPrint( "kein freierPuffer, abbrechen >>>>>>>>>>>>>>>>>>>>>>>>>>>>>" );
    }
    return U8_DCC_BufIdx;
}

//###################### Timer 2 Handling      ##############################

//###################### Definitionen          ##############################
#define BIT0_LEN  (232/2)  // Bitlength in Timer2-Ticks
#define BIT1_LEN  (116/2)
#define SET_BIT0 {OCR2A = BIT0_LEN - 1; OCR2B = BIT0_LEN / 2 - 1;}
#define SET_BIT1 {OCR2A = BIT1_LEN - 1; OCR2B = BIT1_LEN / 2 - 1;}

//###################### Timer 2 init           ##############################
void InitTimer2(void) {
    // fast PWM  mit Top = OCR2A;
    // Ausgangssignal OCR2B ist aktiv
    // Prescaler = 32 (=2µs / Takt)
    SET_BIT1;
    TCCR2A = (1 << COM2B1 )  // COM2B1:0 = 1/0: Clear OC2B on Compare Match, set OC2B at BOTTOM
           | (0 << COM2B0 )    
           | (1 << WGM21 )
           | (1 << WGM20 );  // WGM22:0 = 1/1/1: FastPWM, OCRA=Top
    TCCR2B = (1 << WGM22 )
           | (0 << CS22  )
           | (1 << CS21  )
           | (1 << CS20  );  // CS22:0 = 0/1/1 clk/32 from prescaler
    TIMSK2 = (1 << OCIE2B ); // Interrupt on OCRB match
    
    // DCC-Signal initialisieren
    U8_DCC_State = DCC_PREAMBLE;
    U8_PreambleBitCnt = 0;

    // DDC Output initialisieren
    pinMode( 3, OUTPUT ); // OCR2B gibt auf D3 aus;
}

//###################### Timer 2 interrupt service ##############################
ISR ( TIMER2_COMPB_vect) {
    static byte bitCount;        // Bitzähler (zeigt auf das aktuell ausgegebene Bit )
    static byte byteCount;       // Bytezähler im aktuellen Paket
    static byte *packetBufPtr;   // Zeiger auf das aktuell auszugebende DCC-Paket
    static byte sendIdle;        // Es wird das Idle-Packet gesendet
    
    byte temp;
    // Set OCRA / OCRB for the next Bit
    static const byte bitMsk[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    
    switch ( U8_DCC_State ) {
      case DCC_PREAMBLE : 
        // In der Preamble werden nur 1-Bits ausgegeben (keine Änderung von
        // OCRB notwendig. Solange die Mindestzahl noch nicht erreicht ist, 
        // wird nicht auf neues Paket geprüft
        SET_TP1;
        if ( U8_PreambleBitCnt++ > PREAMBLE_BIT_MIN ) {
            SET_BIT0; //Startbit ausgeben
            bitCount = 8;
            U8_DCC_State = DCC_DATA;
            // prüfen ob neues Telegramm auszugeben ist.
            if ( arU8_DCC_BufState[0] == BUF_WAIT || arU8_DCC_BufState[1] == BUF_WAIT ) {
                // es wartet ein Telegramm auf Ausgabe, Datenpointer setzen
                // drauf achten, dass die Puffer gewechselt werden ( wenn beide auf
                // 'WAIT' stehen
                sendIdle = false;
                temp = U8_DCC_BufSndIdx;
                U8_DCC_BufSndIdx = U8_DCC_BufSndIdx == 0 ? 1 : 0;
                if ( arU8_DCC_BufState[U8_DCC_BufSndIdx] != BUF_WAIT ) U8_DCC_BufSndIdx = temp;
                byteCount = arU8_DCC_BufDataLen[U8_DCC_BufSndIdx];
                packetBufPtr = &arU8_DCC_Buf[U8_DCC_BufSndIdx][0];
                arU8_DCC_BufState[U8_DCC_BufSndIdx] = BUF_OUT;
            } else {
                // kein Telegramm auszugeben, Idle-Telegramm senden
                packetBufPtr = arU8_DCC_PacketIdle;
                byteCount = sizeof(arU8_DCC_PacketIdle);
                sendIdle = true;
            }
        }
        CLR_TP1;
        break;
      case DCC_DATA:
      case DCC_IDLE:
        // Datenbits ausgeben
        if ( bitCount == 0 ) {
          SET_TP2;
          // Byte ist komplett ausgegeben, Stopbit ausgeben
          if ( byteCount == 1) {
            SET_TP3;
            // war letztes Byte, 1 ausgeben und auf Preamble schalten
            SET_BIT1;
            if ( U8_DCC_State == DCC_DATA && !sendIdle )arU8_DCC_BufState[U8_DCC_BufSndIdx] = BUF_SENT; 
            U8_DCC_State = DCC_PREAMBLE;
            U8_PreambleBitCnt = 0;
            CLR_TP3;
          } else {
            // Stopbit ausgeben und auf nächstes Byte schalten
            SET_BIT0;
            bitCount = 8;
            packetBufPtr++;
            byteCount--;
           }
        CLR_TP2;
        } else {
          SET_TP4;
          // Datenbit ausgeben
          if ( *packetBufPtr & bitMsk[--bitCount] ){
            SET_BIT1;
          } else {
            SET_BIT0;
          }
          CLR_TP4;
        }
    }
    
}

