/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Copyright (c) 1997 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Ported from CMU/Monarch's code, nov'98 -Padma.
 * Contributions by:
 *   - Mike Holland
 */

#include <delay.h>
#include <connector.h>
#include <packet.h>
#include <random.h>

#include <backoffscheme.h>

BackoffScheme::~BackoffScheme()
{
}

int
ExponentialBackoff::inc_cw(int cw_min, int cw_max)
{
  cw_ = (cw_ << 1) + 1;
  if (cw_ > cw_max) cw_ = cw_max;

  return cw_;
}

int
ExponentialBackoff::reset_cw(int cw_min, int cw_max)
{
  cw_ = cw_min;
  return cw_;
}

int
FibonacciBackoff::inc_cw(int cw_min, int cw_max)
{
  fib_index++;
  if ( fib_index == 22 ) fib_index = 21;
  cw_ = fibonacci_numbers[fib_index];

  if (cw_ > cw_max) cw_ = cw_max;

  return cw_;
}

int
FibonacciBackoff::reset_cw(int cw_min, int cw_max)
{
  for ( fib_index = 0; (fib_index < 22) && (fibonacci_numbers[fib_index] < (uint32_t)cw_min); fib_index++ );

  if ( fib_index == 22 ) fib_index = 21;
  cw_ = fibonacci_numbers[fib_index];

  return cw_;
}

