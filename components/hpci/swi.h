#ifndef __SWI_H__
#define __SWI_H__
#include <functional> // Include for std::function
#include "esphome.h"

namespace swi
{
/**
 *  Defines
 */
#define PIN 22
#define LOW_TIME 1000
#define HIGH_1_TIME 1000
#define HIGH_0_TIME 3000

#define HIGH_START_FRAME 5000

#define DURATION_MARGIN 600 // Marge d'erreur pour les durées (élargie : mesures réelles jusqu'à 350us d'écart sur le marqueur de trame)

#define MAX_TIME 12000
#define MAX_FRAME_SIZE 20

#define SEND_MSG_OCCURENCE 1

#define MAX_ERROR_COUNT 5 // Threshold for reset

#define DEBOUNCE_THRESHOLD 400 // Debounce threshold in microseconds

// Une rafale complete (trame Dreamline 16 octets) comporte environ 260 fronts.
// 300 laisse de la marge meme si loop() est occupe pendant une rafale entiere.
#define EDGE_BUFFER_SIZE 300
    /**
     *  Enums
     */
    enum wireDirection
    {
        SENDING,
        RECEIVING
    };

    enum communicationState
    {
        IDLE,
        RECEIVING_DATA,
        TRANSMITTING_DATA
    };

    enum receiveState
    {
        START_FRAME,
        IN_FRAME,
        END_FRAME
    };

    /**
     *  Variables
     */
    extern volatile unsigned long triggerTime;
    extern volatile unsigned long lastTriggerTime;

    extern volatile unsigned long triggerDeltaTime;

    extern volatile byte triggerStatus;
    extern volatile byte lastTriggerStatus;

    extern volatile boolean triggered;
    extern volatile wireDirection currentDirection;

    extern volatile boolean frame_available;

    extern uint8_t read_frame[MAX_FRAME_SIZE];
    extern uint8_t frameCnt;

    // File circulaire remplie par l'ISR, videe par readFrame(). Elle evite de perdre
    // des fronts quand le loop() ESPHome (WiFi/API) met plusieurs ms a revenir,
    // ce qui arrivait avec le simple couple de variables triggerDeltaTime/lastTriggerStatus.
    typedef struct
    {
        unsigned long deltaTime;
        uint8_t levelOfInterval; // niveau (HIGH/LOW) de l'intervalle qui vient de se terminer
    } edge_t;

    extern volatile edge_t edgeBuffer[EDGE_BUFFER_SIZE];
    extern volatile uint16_t edgeBufHead; // ecrit uniquement par l'ISR
    extern volatile uint16_t edgeBufTail; // ecrit uniquement par readFrame()
    extern volatile uint16_t edgeBufOverflowCount;

    /**
     *  Functions
     */

    inline unsigned long delaisWithoutRollover(unsigned long t1, unsigned long t2);
    uint8_t reverseBits(unsigned char x);
    void isrCallback(void);
    void setWireDirection(wireDirection direction);

    /* ================================================== */
    void swi_setup(void);
    void swi_loop(void);
    void clear_reception_flags(void);
    void sendHigh(uint16_t ms);
    void sendLow(uint16_t ms);
    void sendBinary0(void);
    void sendBinary1(void);
    void sendHeaderCmdFrame(void);
    void sendSpaceCmdFrame(void);
    void sendSpaceCmdFramesGroup(void);
    boolean sendFrame(uint8_t frame[], uint8_t size);

    /* ================================================== */

    boolean startFrame(void);
    // Prend desormais le delta en parametre (vient de la file, pas de la variable globale
    // qui ne refleterait que le DERNIER front, pas celui en cours de traitement).
    inline uint8_t readBit(unsigned long deltaTime);
    inline boolean silence(void);
    boolean longSilence();
    void processEdge(unsigned long deltaTime, uint8_t levelOfInterval);
    void readFrame();
}
#endif // __SWI_H__
