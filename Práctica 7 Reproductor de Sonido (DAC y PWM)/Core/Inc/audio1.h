#ifndef AUDIO1_H_
#define AUDIO1_H_

#include <stdint.h>

/* AUDIO 1 - ODE TO JOY                                                      */
/* Metodo: DAC                                                               */
/*                                                                            */

#define AUDIO1_LEN 19

static const uint16_t audio1_freqs[AUDIO1_LEN] =
{
    330, 330, 349, 392,
    392, 349, 330, 294,
    262, 262, 294, 330,
    330, 294, 294,
    262, 294, 330, 262
};

static const uint16_t audio1_durations[AUDIO1_LEN] =
{
    500, 500, 500, 500,
    500, 500, 500, 500,
    500, 500, 500, 500,
    500, 500, 1000,
    500, 500, 500, 500
};


/* AUDIO 2 - MARIO THEME                                                     */
/* Metodo: PWM                                                               */

#define AUDIO2_LEN 44

static const uint16_t audio2_freqs[AUDIO2_LEN] =
{
    880, 698, 523, 587, 698,   0,
    698, 587, 523,   0, 698,   0,
   1047, 880, 784, 523, 880, 698,
    523, 587, 698,   0, 698, 587,
    523,   0, 698,   0, 932, 880,
    784, 698,   0, 880, 698, 523,
    880, 698, 831, 698, 523,   0,
    831, 784
};

static const uint16_t audio2_durations[AUDIO2_LEN] =
{
    480, 360, 120, 120, 120, 120,
    480, 120, 120, 120, 120, 120,
    120, 120, 240, 360, 480, 120,
    480, 360, 120, 120, 120, 120,
    480, 120, 120, 120, 120, 120,
    120, 120, 120, 960, 120, 360,
    360, 240, 360, 480, 120, 120,
    120, 160
};

#endif /* AUDIO1_H_ */
