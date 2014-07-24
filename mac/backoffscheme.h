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
 */

#ifndef __backoffscheme_h__
#define __backoffscheme_h__

class BackoffScheme {
 public:
  int cw_;

  BackoffScheme() {
  }
  virtual ~BackoffScheme();

  virtual void config(char *config_string) = 0;
  virtual int inc_cw(int cw_min, int cw_max) = 0;
  virtual int reset_cw(int cw_min, int cw_max) = 0;
};

class ExponentialBackoff : public BackoffScheme {
 public:
  ExponentialBackoff() {}
  ~ExponentialBackoff() {}

  void config(char */*config_string*/) { };
  int inc_cw(int cw_min, int cw_max);
  int reset_cw(int cw_min, int cw_max);
};

static const uint32_t fibonacci_numbers[22] = {0,1,1,2,3,5,8,13,21,34,55,89,144,233,377,610,987,1597,2584,4181,6765,10946};

class FibonacciBackoff : public BackoffScheme {
 public:
  uint32_t fib_index;

  FibonacciBackoff(): fib_index(0) {
  }

  ~FibonacciBackoff() {}

  void config(char */*config_string*/) { };
  int inc_cw(int cw_min, int cw_max);
  int reset_cw(int cw_min, int cw_max);
};

#endif /* __backoffscheme_h__ */

