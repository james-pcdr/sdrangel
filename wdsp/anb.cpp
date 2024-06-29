/*  anb.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2014 Warren Pratt, NR0V
Copyright (C) 2024 Edouard Griffiths, F4EXB Adapted to SDRangel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

warren@wpratt.com

*/

#include "comm.hpp"
#include "anb.hpp"
#include "RXA.hpp"

#define MAX_TAU         (0.002)     // maximum transition time, signal<->zero
#define MAX_ADVTIME     (0.002)     // maximum deadtime (zero output) in advance of detected noise
#define MAX_SAMPLERATE  (1536000)

namespace WDSP {

void ANB::initBlanker(ANB *a)
{
    int i;
    a->trans_count = (int)(a->tau * a->samplerate);
    if (a->trans_count < 2) a->trans_count = 2;
    a->hang_count = (int)(a->hangtime * a->samplerate);
    a->adv_count = (int)(a->advtime * a->samplerate);
    a->count = 0;
    a->in_idx = a->trans_count + a->adv_count;
    a->out_idx = 0;
    a->coef = PI / a->trans_count;
    a->state = 0;
    a->avg = 1.0;
    a->power = 1.0;
    a->backmult = exp(-1.0 / (a->samplerate * a->backtau));
    a->ombackmult = 1.0 - a->backmult;
    for (i = 0; i <= a->trans_count; i++)
        a->wave[i] = 0.5 * cos(i * a->coef);
    memset(a->dline, 0, a->dline_size * sizeof(wcomplex));
}

ANB* ANB::create_anb  (
    int run,
    int buffsize,
    float* in,
    float* out,
    double samplerate,
    double tau,
    double hangtime,
    double advtime,
    double backtau,
    double threshold
)
{
    ANB *a;
    a = new ANB;
    a->run = run;
    a->buffsize = buffsize;
    a->in = in;
    a->out = out;
    a->samplerate = samplerate;
    a->tau = tau;
    a->hangtime = hangtime;
    a->advtime = advtime;
    a->backtau = backtau;
    a->threshold = threshold;
    a->wave = new float[((int)(MAX_SAMPLERATE * MAX_TAU) + 1)];
    a->dline_size = (int)((MAX_TAU + MAX_ADVTIME) * MAX_SAMPLERATE) + 1;
    a->dline = new float[a->dline_size * 2];
    initBlanker(a);
    a->legacy = new float[2048 * 2];  /////////////// legacy interface - remove
    return a;
}

void ANB::destroy_anb (ANB *a)
{
    delete[] (a->legacy);                                                                                      /////////////// legacy interface - remove
    delete[] (a->dline);
    delete[] (a->wave);
    delete (a);
}

void ANB::flush_anb (ANB *a)
{
    a->cs_update.lock();
    initBlanker (a);
    a->cs_update.unlock();
}

void ANB::xanb (ANB *a)
{
    double scale;
    double mag;
    int i;
    if (a->run)
    {
        a->cs_update.lock();
        for (i = 0; i < a->buffsize; i++)
        {
            mag = sqrt(a->in[2 * i + 0] * a->in[2 * i + 0] + a->in[2 * i + 1] * a->in[2 * i + 1]);
            a->avg = a->backmult * a->avg + a->ombackmult * mag;
            a->dline[2 * a->in_idx + 0] = a->in[2 * i + 0];
            a->dline[2 * a->in_idx + 1] = a->in[2 * i + 1];
            if (mag > (a->avg * a->threshold))
                a->count = a->trans_count + a->adv_count;

            switch (a->state)
            {
                case 0:
                    a->out[2 * i + 0] = a->dline[2 * a->out_idx + 0];
                    a->out[2 * i + 1] = a->dline[2 * a->out_idx + 1];
                    if (a->count > 0)
                    {
                        a->state = 1;
                        a->dtime = 0;
                        a->power = 1.0;
                    }
                    break;
                case 1:
                    scale = a->power * (0.5 + a->wave[a->dtime]);
                    a->out[2 * i + 0] = a->dline[2 * a->out_idx + 0] * scale;
                    a->out[2 * i + 1] = a->dline[2 * a->out_idx + 1] * scale;
                    if (++a->dtime > a->trans_count)
                    {
                        a->state = 2;
                        a->atime = 0;
                    }
                    break;
                case 2:
                    a->out[2 * i + 0] = 0.0;
                    a->out[2 * i + 1] = 0.0;
                    if (++a->atime > a->adv_count)
                        a->state = 3;
                    break;
                case 3:
                    if (a->count > 0)
                        a->htime = -a->count;

                    a->out[2 * i + 0] = 0.0;
                    a->out[2 * i + 1] = 0.0;
                    if (++a->htime > a->hang_count)
                    {
                        a->state = 4;
                        a->itime = 0;
                    }
                    break;
                case 4:
                    scale = 0.5 - a->wave[a->itime];
                    a->out[2 * i + 0] = a->dline[2 * a->out_idx + 0] * scale;
                    a->out[2 * i + 1] = a->dline[2 * a->out_idx + 1] * scale;
                    if (a->count > 0)
                    {
                        a->state = 1;
                        a->dtime = 0;
                        a->power = scale;
                    }
                    else if (++a->itime > a->trans_count)
                        a->state = 0;
                    break;
            }
            if (a->count > 0) a->count--;
            if (++a->in_idx == a->dline_size) a->in_idx = 0;
            if (++a->out_idx == a->dline_size) a->out_idx = 0;
        }
        a->cs_update.unlock();
    }
    else if (a->in != a->out)
        memcpy (a->out, a->in, a->buffsize * sizeof (wcomplex));
}

void ANB::setBuffers_anb (ANB *a, float* in, float* out)
{
    a->in = in;
    a->out = out;
}

void ANB::setSamplerate_anb (ANB *a, int rate)
{
    a->samplerate = rate;
    initBlanker (a);
}

void ANB::setSize_anb (ANB *a, int size)
{
    a->buffsize = size;
    initBlanker (a);
}

/********************************************************************************************************
*                                                                                                       *
*                                             RXA PROPERTIES                                            *
*                                                                                                       *
********************************************************************************************************/

void ANB::SetRXAANBRun (RXA& rxa, int run)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->run = run;
    a->cs_update.unlock();
}

void ANB::SetRXAANBBuffsize (RXA& rxa, int size)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->buffsize = size;
    a->cs_update.unlock();
}

void ANB::SetRXAANBSamplerate (RXA& rxa, int rate)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->samplerate = (double) rate;
    initBlanker (a);
    a->cs_update.unlock();
}

void ANB::SetRXAANBTau (RXA& rxa, double tau)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->tau = tau;
    initBlanker (a);
    a->cs_update.unlock();
}

void ANB::SetRXAANBHangtime (RXA& rxa, double time)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->hangtime = time;
    initBlanker (a);
    a->cs_update.unlock();
}

void ANB::SetRXAANBAdvtime (RXA& rxa, double time)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->advtime = time;
    initBlanker (a);
    a->cs_update.unlock();
}

void ANB::SetRXAANBBacktau (RXA& rxa, double tau)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->backtau = tau;
    initBlanker (a);
    a->cs_update.unlock();
}

void ANB::SetRXAANBThreshold (RXA& rxa, double thresh)
{
    ANB *a = rxa.anb.p;
    a->cs_update.lock();
    a->threshold = thresh;
    a->cs_update.unlock();
}

}

