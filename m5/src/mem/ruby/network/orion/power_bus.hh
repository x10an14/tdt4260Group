/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _POWER_BUS_H
#define _POWER_BUS_H

typedef enum {
        RESULT_BUS = 1,
        GENERIC_BUS,
        BUS_MAX_MODEL
} power_bus_model;

typedef enum {
        IDENT_ENC = 1,  /* identity encoding */
        TRANS_ENC,      /* transition encoding */
        BUSINV_ENC,     /* bus inversion encoding */
        BUS_MAX_ENC
} power_bus_enc;


typedef struct {
        int model;
        int encoding;
        unsigned data_width;
        unsigned grp_width;
        unsigned long int n_switch;
        double e_switch;
        /* redundant field */
        unsigned bit_width;
        unsigned long int bus_mask;
} power_bus;

extern int power_bus_init(power_bus *bus, int model, int encoding, unsigned width, unsigned grp_width, unsigned n_snd, unsigned n_rcv, double length, double time);

extern int bus_record(power_bus *bus, unsigned long int old_state, unsigned long int new_state);

extern double bus_report(power_bus *bus);

#endif
