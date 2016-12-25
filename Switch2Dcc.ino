//#include <MobaTools.h>

/*  Stellpult - Zentrale
 *  
 *  Aufgrund der angeschlossenen Weichenschalter wird ein Dcc-Signal erzeugt
 *  um entsprechende Weichendecoder ansteuern zu können.
 *  Das DCC-Signal wird per Timer2 erzeugt mit fast PWM, Prescaler 32. Pro Bit ist ein Irq notwendig
 *  Der Irq wird per OCRB in der Mitte eines Bits ausgelöst und setzt die Timerwerte für das nächste Bit.
 *  
*/
#define WEICHEN_VERSION_ID 02
// Schaltermatrix
// Bitcodierte Schalter ( max 4x4 oder 4x8 = 16/32 Schalter )
// je nachdem ob die _Schalterbits in einem int(16Bit) oder long(32Bit) gespeichert werden
// Matrix-Input (Col): Pin 4-7 
// Matrix Output(Row): Pin 8-11
// Adressen der zu schaltenden Weichen und Row/column des zugehörinen Schalters (Matrix)
const uint8_t weicheAddr[]    = { 1, 2,  3,  4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16 };
const uint8_t weicheColsP[]   = { 4, 5,  6,  7, 4, 5,  6,  5,  4,  5,  6,  7,  4,  5,  6,  7 };
const uint8_t weicheRowsP[]   = { 8, 8,  8,  8, 9, 9,  9,  9, 10, 10, 10, 10, 11, 11, 11, 11 };
uint8_t       weicheStat[ sizeof(weicheAddr) ]; // Bit0=Weichenposition, Bit1: Flag 'Telegram erzeugen'
#define POSMSK  1
#define TELMSK  2

// Variable für die Ausgabe der DCC-Pakete
byte DCC_State;       // Status der Ausgabe
#define DCC_PREAMBLE  0   // gibt nur 1-Bits aus
#define DCC_DATA      1   // aktuelles Paket ausgeben
#define DCC_IDLE      2   // Idle-Telegramm wird ausgegeben

// Für die Datentelegramme ( Weichentelegramme ) gibt es einen Wechselpuffer. Während ein Puffer in 
// der ISR ausgegeben wird, kann der andere gefüllt werden
byte dccBufState[2];   // aktueller Pufferstatus
#define BUF_IDLE 0    // Puffer inaktiv
#define BUF_WRITE 1   // Puffer wird gerade mit Daten beschrieben
#define BUF_WAIT  2   // Daten gültig, warten auf Ausgabe
#define BUF_OUT   3   // Puffer wird auf DCC-Ausgang geschrieben
#define BUF_SENT  4   // Puffer wurde komplett auf dcc ausgegeben
byte dccBufLen[2];    // atuelle Datenlänge (incl. Prüfsumme)
byte dccBufRep[2];    // Wiederholungen bei der ausgabe
#define REPEATS 2
byte dccBufData[2][10];// Datenpuffer, incl Prüfsumme
byte dccBufWrIx;      // Schreibindex ( es kann immer nur in einen Puffer geschrieben werden
int8_t dccBufIsr;     // Puffer der gerade per ISR ausgegeben wird (=<0, wenn keine ausgabe aktiv)

// Variable für die ISR-Routine zur dcc-Ausgabe
const byte preambleBitMin = 14;
byte preambleBitCount;
byte Dcc_Idle[] = { 0xff,0,0xff }; // Idle pcket. Wird immer ausgegeben, wenn am ende der Preamble kein
                                   // Ausgabepuffer bereit steht.


// für debugging ------------------------------------------------------------
// Debug-Ports
#define debug
#include "debugports.cpp"


//###################### Ende der Definitionen ##############################
//###########################################################################
void setup() {
  #ifdef debug
  Serial.begin( 115200 );
  #endif
  
  // Ports für das Einlesen der Schalter initiieren ( Matrix )
  for ( byte i=0; i<sizeof(weicheRowsP) ; i++ ) {
      // in weicheRowsP stehen die Outputports der Matrix
      pinMode( weicheRowsP[i], OUTPUT );
      digitalWrite(  weicheRowsP[i], HIGH );
  }
  
  for ( byte i=0; i<sizeof(weicheColsP); i++ ) {
      // Eingänge der Matrix aals Eingänge mit Pullup
      pinMode( weicheColsP[i], INPUT_PULLUP );
  }
  // Schalter einlesen und alle auf 'geaendert' stellen, so dass beim Programmstart
  // die entsprechenden Weichenbefehle ausgegeben werden.
  DebugPrint( "IniSwitches: " );
  for ( byte i=0; i<sizeof(weicheStat); i++ ) {
    weicheStat[i] = TELMSK | getSwitch(i);
    DebugPrint( ",%d",weicheStat[i]&1 );
  }
  DebugPrint( "\n\r----------\n\r");
  InitTimer2();
  MODE_TP1;
  MODE_TP2;
  MODE_TP4;
  MODE_TP3;
}
////////////////////////////////////////////////////////////////
uint32_t getSwitch(byte swIx) {
    digitalWrite( weicheRowsP[swIx], LOW );
    byte pos =  digitalRead( weicheColsP[swIx]);
    digitalWrite( weicheRowsP[swIx], HIGH );
    return pos;
}
////////////////////////////////////////////////////////////////
void loop() {
    byte turnoutDir;
    // Schalter einlesen 
    for ( byte i=0; i<sizeof( weicheStat ); i++ ) {
        // wenn Schalterstellung != weichenstatus, dann Telegrammbit setzen
        byte pos = getSwitch(i);
        if ( pos != (weicheStat[i] & POSMSK) ) {
            // Schalterstatus hat sich geändert
            weicheStat[i] = pos | TELMSK;
            DebugPrint( "Index: %d, Weiche %d, Status: %d\n\r", i, weicheAddr[i], weicheStat[i] );
        }
    }      
    // für geänderte Weichen jeweils ein Telegramm erzeugen
    for ( byte i=0; i<sizeof( weicheStat ); i++ ) {
        // Tel-Bits in weicheStat abfragen
        if ( (weicheStat[i] & TELMSK) != 0 ) {
            // Schalter wurde betätigt, Telegramm erzeugen
            turnoutDir = (weicheStat[i] & POSMSK) != 0; // aktuelle Schalterposition
            //DebugPrint( "Switch[%d/%d]->WAdr %d\n\r", i, turnoutDir, weicheAddr[i] );
            if ( CreateTurnout( weicheAddr[i] , turnoutDir ) >= 0 ) {
                // Telegramm wurde erzeugt, Tel-Bit löschen
                weicheStat[i] &= ~( TELMSK );
            }
        }
    }
    
    // Telegramm Wiederholungen verwalten
    for ( byte i= 0; i<=2; i++ ) {
        if ( dccBufState[i] == BUF_SENT ) {
            // Puffer wurde gesendet, wiederholen oder freigeben
            // DebugPrint( "Buffer[%d] gesendet, RepCnt=%d\n\r", i ,dccBufRep[i] );
            if ( dccBufRep[i] > 1 ) {
                // Telegramm erneut senden
                dccBufRep[i]--;
                dccBufState[i] = BUF_WAIT;
            } else {
                dccBufState[i] = BUF_IDLE;
            }
        }
    }
    delay(5);
}

//////////////////////////////////////////////////////////////
// Weichentelegramm erzeugen
int8_t CreateTurnout( int turnoutNo, byte direction ) {
    // Es wird ein Weichentelegramm für die übergebene Weichennummer erzeugt. Funktionswert ist der Puffer
    // oder -1 wenn kein freier Puffer vorhanden ist
    byte bufIx, bufNo, chksum;
    int dccAddr;
    // Weichenadressen zählen ab 1, die DCC Adressierung ab 0
    // Standardmässig werden die ersten 4 Adressen nicht verwendet, d.h
    // Weichenadresse 1 -> dccadresse 4
    dccAddr=  turnoutNo > 0 ? turnoutNo+3 : 4 ;
    for ( bufNo = 0; bufNo <= 1; bufNo++ ) {
        // freien Puffer suchen
        if ( dccBufState[bufNo] == BUF_IDLE ) break;
    }
    if ( bufNo > 1 ) return -1; // kein freierPuffer, abbrechen >>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    dccBufState[bufNo] = BUF_WRITE;
    dccBufRep[bufNo] = REPEATS; // Weichentelegramme REPEATS x wiederholen
    bufIx = 0;
    chksum = 0;
    // dcc-Adressbyte bestimmen
    // 1. Byte is b10xxxxxx , wobei x die Bits 7-2 der Weichenadresse sind
    dccBufData[bufNo][bufIx] = 0b10000000 | ( (dccAddr>>2) & 0b00111111);
    chksum ^= dccBufData[bufNo][bufIx++];
    // 2. Byte setzt sich zusammen aus der High-Adresse, den untersten 2 Bits der Weichenadresse
    // und der Spulenadresse
    dccBufData[bufNo][bufIx] =  0b10000000 |
                                  ((((dccAddr>>8) & 0x07 ) ^0xff) << 4 ) |
                                  ( 1 << 3 ) |
                                  (( dccAddr & 0x03 ) << 1 ) |
                                  ( direction & 0x01 );
    chksum ^= dccBufData[bufNo][bufIx++];
    dccBufData[bufNo][bufIx++] = chksum;
    dccBufLen[bufNo] = bufIx;
    dccBufState[bufNo] = BUF_WAIT;
    DebugPrint( "DCCBuf[%d]: Weiche=%d, DccAdr=%d, Rep:%d Data: %x:%x:%x\n\r", bufNo, turnoutNo, dccAddr, dccBufRep[bufNo],dccBufData[bufNo][0],dccBufData[bufNo][1],dccBufData[bufNo][2]);
    return bufNo;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////   
// Unterprogramme für Timer 2
#define BIT0_LEN  (232/2)  // Bitlength in Timer2-Ticks
#define BIT1_LEN  (116/2)
#define SET_BIT0 {OCR2A = BIT0_LEN-1; OCR2B = BIT0_LEN/2-1;}
#define SET_BIT1 {OCR2A = BIT1_LEN-1; OCR2B = BIT1_LEN/2-1;}

void InitTimer2(void) {
    // Timer 2 initiieren:
    // fast PWM  mit Top = OCR2A;
    // Ausgangssignal OCR2B ist aktiv
    // Prescaler = 32 (=2µs / Takt)
    SET_BIT1;
    TCCR2A = (1<<COM2B1 )  // COM2B1:0 = 1/0: Clear OC2B on Compare Match, set OC2B at BOTTOM
           | (0<<COM2B0 )    
           | (1<< WGM21 )
           | (1<< WGM20 ); // WGM22:0 = 1/1/1: FastPWM, OCRA=Top
    TCCR2B = (1<< WGM22 )
           | (0<< CS22  )
           | (1<< CS21  )
           | (1<< CS20  ); // CS22:0 = 0/1/1 clk/32 from prescaler


    TIMSK2 = (1<<OCIE2B );  // Interrupt on OCRB match
    // DCC-Signal initiieren
    DCC_State = DCC_PREAMBLE;
    preambleBitCount = 0;
    pinMode( 3, OUTPUT ); // OCR2B gibt auf D3 aus;
}

ISR ( TIMER2_COMPB_vect) {
    static byte bitCount;        // Bitzähler (zeigt auf das aktuell ausgegebene Bit )
    static byte byteCount;       // Bytezähler im aktuellen Paket
    static byte *packetBufPtr;   // Zeiger auf das aktuell auszugebende DCC-Paket
    static byte sendIdle;        // Es wird das Idle-Packet gesendet
    
    byte temp;
    // Set OCRA / OCRB for the next Bit
    static const byte bitMsk[] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
    switch ( DCC_State ) {
      case DCC_PREAMBLE : 
        // In der Preamble werden nur 1-Bits ausgegeben (keine Änderung von
        // OCRB notwendig. Solange die Mindestzahl noch nicht erreicht ist, 
        // wird nicht auf neues Paket geprüft
        SET_TP1;
        if ( preambleBitCount++ > preambleBitMin ) {
            SET_BIT0; //Startbit ausgeben
            bitCount = 8;
            DCC_State = DCC_DATA;
            // prüfen ob neues Telegramm auszugeben ist.
            if ( dccBufState[0] == BUF_WAIT || dccBufState[1] == BUF_WAIT ) {
                // es wartet ein Telegramm auf Ausgabe, Datenpointer setzen
                // drauf achten, dass die Puffer gewechselt werden ( wenn beide auf
                // 'WAIT' stehen
                sendIdle = false;
                temp = dccBufIsr;
                dccBufIsr = dccBufIsr == 0 ? 1 : 0;
                if ( dccBufState[dccBufIsr] != BUF_WAIT ) dccBufIsr = temp;
                byteCount = dccBufLen[dccBufIsr];
                packetBufPtr = &dccBufData[dccBufIsr][0];
                dccBufState[dccBufIsr] = BUF_OUT;
            } else {
                // kein Telegramm auszugeben, Idle-Telegramm senden
                packetBufPtr = Dcc_Idle;
                byteCount = sizeof(Dcc_Idle);
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
            if ( DCC_State == DCC_DATA && !sendIdle )dccBufState[dccBufIsr] = BUF_SENT; 
            DCC_State = DCC_PREAMBLE;
            preambleBitCount = 0;
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

