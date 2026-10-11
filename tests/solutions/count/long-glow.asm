; ON and SET take 2 ticks, the loop 2 x 11 = 22, WAIT 1: 25 on.
.machine COUNT
        ON
        SET 11
again:  DEC
        JNZ again
        WAIT
        OFF
        HALT
