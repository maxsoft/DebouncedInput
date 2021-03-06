/*
 * Copyright (c) 2013, Majenko Technologies
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 * 
 * * Neither the name of Majenko Technologies nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <DebouncedInput.h>

DebouncedInput::DebouncedInput(byte pin, unsigned long dbt, boolean pullup = true, uint8_t keep)
{
    _pin = pin;
    _debounceTime = dbt;
    _pullup = pullup;
    _keep = keep;
    if (_keep > 0) {
        _lastHigh = (unsigned long *)malloc(_keep * sizeof(unsigned long));
        _lastLow = (unsigned long *)malloc(_keep * sizeof(unsigned long));
        for (int i = 0; i < _keep; i++) {
            _lastHigh[i] = _lastLow[i] = 0;
        }
    } else {
        _lastHigh = NULL;
        _lastLow = NULL;
    }
}

void DebouncedInput::begin()
{
    if (_pullup) {
        pinMode(_pin, INPUT_PULLUP);
    } else {
        pinMode(_pin, INPUT);
    }
    _value = digitalRead(_pin);
    _lastValue = _value;
}

int DebouncedInput::read() 
{
    int r;
    unsigned long now = millis();
    r = digitalRead(_pin);
    if (r != _lrt) {
        _lrt = r;
        _lastChange = now;
    }
    if (now - _lastChange > _debounceTime) {
        if (_value != _lrt) {
            _value = _lrt;
            if (_keep > 0) {
                if (_value) {
                    for (int i = _keep-1; i > 0; i--) {
                        _lastHigh[i] = _lastHigh[i-1];
                    }
                    _lastHigh[0] = now;
                } else {
                    for (int i = _keep-1; i > 0; i--) {
                        _lastLow[i] = _lastLow[i-1];
                    }
                    _lastLow[0] = now;
                }
            }
        }
    }
    return _value;
}

boolean DebouncedInput::changed()
{
    int r = read();
    if (r != _lastValue) {
        _lastValue = r;
        return true;
    }
    return false;
}

boolean DebouncedInput::changed(uint8_t *val) {
    if (changed()) {
        *val = _lastValue;
        return true;
    }
    return false;
}

boolean DebouncedInput::changedTo(uint8_t val) {
    if (changed()) {
        return val == _lastValue;
    }
    return false;
}

unsigned long DebouncedInput::getHighTime(uint8_t seq) {
    if (seq >= _keep) {
        return 0;
    }
    return _lastHigh[seq];
}

unsigned long DebouncedInput::getLowTime(uint8_t seq) {
    if (seq >= _keep) {
        return 0;
    }
    return _lastLow[seq];
}

void DebouncedInput::clearTimes() {
    if (_keep == 0) {
        return;
    }
    for (int i = 0; i < _keep; i++) {
        _lastLow[i] = _lastHigh[i] = 0;
    }
}

#if defined(__PIC32MX__) || defined(__PIC32MZ__)
struct CNInputs {
    DebouncedInput *in;
    struct CNInputs *next;
};

struct CNInputs *ChangeNotificationInputList = NULL;

void __attribute__((interrupt)) DebouncedInputChangeNotificationHandler() {
    struct CNInputs *scan;

    for (scan = ChangeNotificationInputList; scan; scan = scan->next) {
        scan->in->callOnChange();
    }
#ifdef _CHANGE_NOTICE_IRQ
    clearIntFlag(_CHANGE_NOTICE_IRQ);
#endif
#ifdef _CHANGE_NOTICE_A_IRQ
    clearIntFlag(_CHANGE_NOTICE_A_IRQ);
#endif
#ifdef _CHANGE_NOTICE_B_IRQ
    clearIntFlag(_CHANGE_NOTICE_B_IRQ);
#endif
#ifdef _CHANGE_NOTICE_C_IRQ
    clearIntFlag(_CHANGE_NOTICE_C_IRQ);
#endif
}

void DebouncedInput::callOnChange() {
    int r = digitalRead(_pin);
    if (r != _lastValue) {
        _lastValue = r;
        if (r == LOW && _intDir == FALLING) {
            if ((_intTime == 0) || ((millis() - _intTime) > _debounceTime)) {
                _intTime = millis();
                _onChange(_lastValue);
            }
        } else if (r == HIGH && _intDir == RISING) {
            if ((_intTime == 0) || ((millis() - _intTime) > _debounceTime)) {
                _intTime = millis();
                _onChange(_lastValue);
            }
        }
    }
}

void DebouncedInput::attachInterrupt(void (*func)(int), int dir) {

#if defined(_CHANGE_NOTICE_IRQ)
    int cn = digitalPinToCN(_pin);

    if (cn == NOT_CN_PIN) {
        return;
    }

    CNENSET = cn;
    CNCONbits.ON    =   1;
    CNCONbits.SIDL  =   0;
    setIntVector(_CHANGE_NOTICE_VECTOR, DebouncedInputChangeNotificationHandler);
    setIntPriority(_CHANGE_NOTICE_VECTOR, 1, 0);
    clearIntFlag(_CHANGE_NOTICE_IRQ);
    setIntEnable(_CHANGE_NOTICE_IRQ);
#else

    volatile p32_ioport *   iop;
    int port, bit;
    if ((port = digitalPinToPort(_pin)) == NOT_A_PIN)
    {
        return;
    }

    //* Obtain pointer to the registers for this io port.
    iop = (p32_ioport *)portRegisters(port);

    //* Obtain bit mask for the specific bit for this pin.
    bit = digitalPinToBitMask(_pin);

    iop->cnen.set = bit;
    iop->cncon.set = 1<<15;
    setIntVector(_CHANGE_NOTICE_VECTOR, DebouncedInputChangeNotificationHandler);
    setIntPriority(_CHANGE_NOTICE_VECTOR, 1, 0);
    if (port == _IOPORT_PA) {
        clearIntFlag(_CHANGE_NOTICE_A_IRQ);
        setIntEnable(_CHANGE_NOTICE_A_IRQ);
    }
    if (port == _IOPORT_PB) {
        clearIntFlag(_CHANGE_NOTICE_B_IRQ);
        setIntEnable(_CHANGE_NOTICE_B_IRQ);
    }
    if (port == _IOPORT_PC) {
        clearIntFlag(_CHANGE_NOTICE_C_IRQ);
        setIntEnable(_CHANGE_NOTICE_C_IRQ);
    }
#endif
    _onChange = func;
    _intDir = dir;
    _intTime = 0;

    if (ChangeNotificationInputList == NULL) {
        ChangeNotificationInputList = (struct CNInputs *)malloc(sizeof(struct CNInputs));
        ChangeNotificationInputList->in = this;
        ChangeNotificationInputList->next = NULL;
        return;
    }

    struct CNInputs *scan;
    for (scan = ChangeNotificationInputList; scan->next; scan = scan->next);
    scan->next = (struct CNInputs *)malloc(sizeof(struct CNInputs));
    scan->next->next = NULL;
    scan->next->in = this;
}

#endif
