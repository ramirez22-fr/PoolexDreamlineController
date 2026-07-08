#include "swi.h"

namespace swi
{
    volatile unsigned long triggerTime = 0;
    volatile unsigned long lastTriggerTime = 0;

    volatile unsigned long triggerDeltaTime;

    volatile byte triggerStatus;
    volatile byte lastTriggerStatus;

    volatile boolean triggered = false;

    volatile wireDirection currentDirection = RECEIVING;

    volatile boolean frame_available = false;

    uint8_t read_frame[MAX_FRAME_SIZE];
    uint8_t frameCnt;
    unsigned long lastLoopTime = 0;

    volatile communicationState swi_state = IDLE;
    receiveState swi_receive_state = START_FRAME;
    static uint8_t error_count = 0;

    // File circulaire d'evenements de front, remplie par l'ISR / videe par readFrame().
    volatile edge_t edgeBuffer[EDGE_BUFFER_SIZE];
    volatile uint16_t edgeBufHead = 0;
    volatile uint16_t edgeBufTail = 0;
    volatile uint16_t edgeBufOverflowCount = 0;

    IRAM_ATTR void isrCallback(void)
    {
        unsigned long currentMicros = micros();
        unsigned long delta = delaisWithoutRollover(triggerTime, currentMicros);
        uint8_t endedLevel = triggerStatus; // niveau qui vient de se terminer (avant ce front)

        // Variables scalaires conservees pour compat (silence() s'appuie sur triggerTime,
        // et le log de diagnostic sur triggerDeltaTime/lastTriggerStatus).
        lastTriggerTime = triggerTime;
        triggerTime = currentMicros;
        triggerDeltaTime = delta;
        lastTriggerStatus = endedLevel;
        triggerStatus = digitalRead(PIN);
        triggered = true;

        // Empile l'evenement. Si le buffer est plein (loop() vraiment tres en retard),
        // on ecrase le plus ancien non lu plutot que de bloquer l'ISR, et on compte
        // l'incident pour que ce soit visible dans les logs plutot que silencieux.
        uint16_t nextHead = (uint16_t)((edgeBufHead + 1) % EDGE_BUFFER_SIZE);
        if (nextHead == edgeBufTail)
        {
            edgeBufOverflowCount++;
            edgeBufTail = (uint16_t)((edgeBufTail + 1) % EDGE_BUFFER_SIZE);
        }
        edgeBuffer[edgeBufHead].deltaTime = delta;
        edgeBuffer[edgeBufHead].levelOfInterval = endedLevel;
        edgeBufHead = nextHead;
    }

    void setWireDirection(wireDirection direction)
    {
        if (direction == RECEIVING)
        {
            ESP_LOGI("SWI", "Setting wire direction to RECEIVING");
            pinMode(PIN, INPUT);
            attachInterrupt(digitalPinToInterrupt(PIN), isrCallback, CHANGE);
            currentDirection = RECEIVING;
        }
        else if (direction == SENDING)
        {
            ESP_LOGI("SWI", "Setting wire direction to SENDING");
            detachInterrupt(digitalPinToInterrupt(PIN));
            pinMode(PIN, OUTPUT);
            currentDirection = SENDING;
        }
        else
        {
            ESP_LOGE("SWI", "Invalid wire direction specified.");
        }
    }

    void swi_setup()
    {
        clear_reception_flags();
        error_count = 0;
        swi_state = IDLE;
        frame_available = false;
        edgeBufHead = 0;
        edgeBufTail = 0;
        edgeBufOverflowCount = 0;
        setWireDirection(RECEIVING);
        ESP_LOGI("SWI", "SWI setup complete.");
    }

    void clear_reception_flags()
    {
        // ESP_LOGI("SWI", "Clearing reception flags...");
        triggerTime = 0;
        lastTriggerTime = 0;
        triggerDeltaTime = 0;
        triggerStatus = LOW;
        lastTriggerStatus = LOW;
        triggered = false;
        frameCnt = 0;
        swi_receive_state = START_FRAME;
    }

    void swi_loop()
    {

        readFrame();

        if (error_count > MAX_ERROR_COUNT)
        {
            error_count = 0;
            ESP_LOGW("SWI", "Error count exceeded! Resetting connection.");
            swi_setup();
            return;
        }
    }

    /**
     * @brief
     *
     * @param t1
     * @param t2
     * @return unsigned long
     */
    inline unsigned long delaisWithoutRollover(unsigned long t1, unsigned long t2)
    {
        // Les durées sont calculées en utilisant la fonction micros(), qui déborde.
        // t2 doit "normalement être plus grand que t1, mais lorsque micros déborde, c'est faux.
        return (t2 > t1) ? (t2 - t1) : ((unsigned long)(-1)) - (t1 - t2);
    }

    /**
     * @brief
     *
     * @param x
     * @return uint8_t
     */
    uint8_t reverseBits(unsigned char x)
    {
        x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
        x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
        x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
        return x;
    }

    /**
     * @brief
     *
     * @param ms
     */
    void sendHigh(uint16_t ms)
    {
        digitalWrite(PIN, HIGH);
        delayMicroseconds(ms * 1000);
    }

    /**
     * @brief
     *
     * @param ms
     */
    void sendLow(uint16_t ms)
    {
        digitalWrite(PIN, LOW);
        delayMicroseconds(ms * 1000);
    }

    /**
     * @brief
     *
     */
    void sendBinary0(void)
    {
        sendLow(1);
        sendHigh(1);
    }

    /**
     * @brief
     *
     */
    void sendBinary1(void)
    {
        sendLow(1);
        sendHigh(3);
    }

    /**
     * @brief
     *
     */
    void sendHeaderCmdFrame(void)
    {
        sendLow(9);
        sendHigh(5);
    }

    /**
     * @brief
     *
     */
    void sendSpaceCmdFrame(void)
    {
        sendLow(1);
        sendHigh(100);
    }

    /**
     * @brief
     *
     */
    void sendSpaceCmdFramesGroup(void)
    {
        sendLow(1);
        // to avoid software watchdog reset due to the long 2000ms delay, we cut the 2000ms in 4x500ms and feed the wdt each time.
        for (uint8_t i = 0; i < 4; i++)
        {
            esphome::App.feed_wdt(); // Feed the watchdog
            sendHigh(500);
        }
    }

    /**
     * @brief
     *
     * @param frame
     * @param size
     */
    boolean sendFrame(uint8_t frame[], uint8_t size)
    {
        if (swi_state == IDLE)
        {
            uint8_t frame_send[16];
            setWireDirection(SENDING);
            swi_state = TRANSMITTING_DATA;
            for (uint8_t i = 0; i < size; i++)
            {
                frame_send[i] = reverseBits(frame[i]); // 1's complement before sending
            }
            for (uint8_t occurrence = 0; occurrence < SEND_MSG_OCCURENCE; occurrence++)
            {
                sendHeaderCmdFrame();
                for (uint8_t frameIndex = 0; frameIndex < size; frameIndex++)
                {
                    uint8_t value = frame_send[frameIndex];
                    for (uint8_t bitIndex = 0; bitIndex < 8; bitIndex++)
                    {
                        uint8_t bit = (value << bitIndex) & B10000000;
                        if (bit)
                        {
                            sendBinary1();
                        }
                        else
                        {
                            sendBinary0();
                        }
                    }
                }

                sendSpaceCmdFramesGroup();
            }
            ESP_LOGI("SWI", "Successfully sent frame !");
            setWireDirection(RECEIVING);
            swi_state = IDLE;
            return true;
        }
        else
        {
            return false;
        }
    }

    inline uint8_t readBit(unsigned long deltaTime)
    {
        if (deltaTime > (HIGH_1_TIME - DURATION_MARGIN) && deltaTime < (HIGH_1_TIME + DURATION_MARGIN))
        {
            return 1;
        }
        if (deltaTime > (HIGH_0_TIME - DURATION_MARGIN) && deltaTime < (HIGH_0_TIME + DURATION_MARGIN))
        {
            return 0;
        }

        error_count++;
        ESP_LOGW("SWI", "Incompatible duration! Count: %d (delta=%lu)", error_count, deltaTime);
        return 0xff; // Invalid bit
    }

    inline boolean silence()
    {
        cli();
        unsigned long delta = delaisWithoutRollover(triggerTime, micros());
        sei();
        return delta > MAX_TIME;
    }

    // essaye de détecter le silence de 1s. A vérifier.
    boolean longSilence()
    {
        if (frameCnt < 9)
            return false;
        return read_frame[0] == 0xd1 && read_frame[1] == 0xb1;
    }

    // Traite UN evenement de front deja depile de edgeBuffer. Remplace la logique
    // qui lisait auparavant directement triggerDeltaTime/lastTriggerStatus : ici la
    // valeur vient en parametre, donc aucun front consomme depuis le buffer n'est
    // jamais ignore, meme si plusieurs sont en attente au meme passage de readFrame().
    void processEdge(unsigned long deltaTime, uint8_t levelOfInterval)
    {
        static boolean startByte = true;
        static uint8_t newByte = 0;
        static uint8_t cptByte = 0;

        if (swi_receive_state == START_FRAME)
        {
            if (levelOfInterval == HIGH)
            {
                if ((deltaTime > (HIGH_START_FRAME - DURATION_MARGIN)) && (deltaTime < (HIGH_START_FRAME + DURATION_MARGIN)))
                {
                    frameCnt = 0;
                    ESP_LOGI("SWI", "Receiving frame... (start marker %lu us)", deltaTime);
                    swi_receive_state = IN_FRAME;
                    swi_state = RECEIVING_DATA;
                    startByte = true;
                    newByte = 0;
                    cptByte = 0;
                }
            }
        }
        else if (swi_receive_state == IN_FRAME)
        {
            if (levelOfInterval == HIGH)
            {
                if (startByte)
                {
                    newByte = 0;
                    cptByte = 0;
                    startByte = false;
                }

                uint8_t bit = readBit(deltaTime);
                if (bit == 0xff)
                {
                    // Duree incompatible avec un bit 0 ou 1 : on abandonne la trame en cours
                    // plutot que d'accumuler un octet corrompu silencieusement.
                    ESP_LOGW("SWI", "Frame aborted at byte %d (bad bit duration %lu us)", frameCnt, deltaTime);
                    clear_reception_flags();
                    return;
                }
                newByte |= bit << cptByte++;
                if (cptByte == 8)
                {
                    startByte = true;
                    read_frame[frameCnt] = newByte;
                    ESP_LOGD("SWI", "Byte #%d = 0x%02X", frameCnt, newByte);
                    frameCnt++;

                    if (frameCnt >= MAX_FRAME_SIZE)
                    {
                        ESP_LOGE("SWI", "Frame overflow detected. Resetting frame counter.");
                        error_count++;
                        clear_reception_flags();
                    }
                }
            }
            // Les intervalles LOW entre deux bits ont une duree fixe (~1ms) qui ne code
            // aucune information : on les ignore volontairement ici.
        }
    }

    void readFrame()
    {
        // Log de diagnostic, au plus 1x/s : etat courant, nombre d'octets deja accumules,
        // profondeur de la file d'evenements en attente, et compteur d'overflow eventuel.
        static unsigned long lastLogTime = 0;
        unsigned long currentTime = millis();
        if (currentTime - lastLogTime >= 1000)
        {
            uint16_t pending = (uint16_t)((edgeBufHead + EDGE_BUFFER_SIZE - edgeBufTail) % EDGE_BUFFER_SIZE);
            ESP_LOGD("SWI", "state=%d frameCnt=%d pending_edges=%u overflow=%u last_delta=%lu",
                     swi_receive_state, frameCnt, pending, edgeBufOverflowCount, triggerDeltaTime);
            lastLogTime = currentTime;
        }

        // On depile TOUS les evenements en attente avant de rendre la main au loop()
        // ESPHome, pour rattraper d'un coup un loop() qui aurait ete occupe (WiFi/API)
        // pendant toute une rafale de ~250-500ms.
        uint16_t processed = 0;
        while (edgeBufTail != edgeBufHead)
        {
            unsigned long deltaTime = edgeBuffer[edgeBufTail].deltaTime;
            uint8_t levelOfInterval = edgeBuffer[edgeBufTail].levelOfInterval;
            edgeBufTail = (uint16_t)((edgeBufTail + 1) % EDGE_BUFFER_SIZE);
            processEdge(deltaTime, levelOfInterval);
            processed++;
        }
        if (processed > 5)
        {
            ESP_LOGD("SWI", "Rattrape %u evenements en un seul passage de loop()", processed);
        }

        // Detection de fin de trame par silence : ne depend pas d'un nouveau front,
        // donc doit rester scrutee, pas evenementielle.
        if (swi_receive_state == IN_FRAME && silence())
        {
            swi_receive_state = END_FRAME;
        }

        if (swi_receive_state == END_FRAME)
        {
            frame_available = true;
            ESP_LOGI("SWI", "Frame received! (%d octets)", frameCnt);
            swi_state = IDLE;
            // Important : on NE remet PAS frameCnt/read_frame a zero ici. hpci::loop()
            // doit encore les lire (swi::frameCnt) juste apres cet appel ; c'etait la
            // cause du "Frame size is not valid (0)" observe jusqu'ici, puisque
            // clear_reception_flags() les remettait a 0 avant meme que HPCI y touche.
            // Le prochain "Receiving frame..." remet frameCnt a 0 lui-meme (voir plus haut).
            swi_receive_state = START_FRAME;
        }
    }

}
