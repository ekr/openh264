/*!
 * \copy
 *     Copyright (c)  2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef WELS_DECODE_MB_AUX_H__
#define WELS_DECODE_MB_AUX_H__

#include "typedefs.h"
#include "macros.h"

namespace WelsDec {

void_t InitDctClipTable (void_t);

void_t IdctResAddPred_c (uint8_t* pPred, const int32_t kiStride,
                         int16_t* pRs);

#if defined(__cplusplus)
extern "C" {
#endif//__cplusplus

#if defined(X86_ASM)
void_t IdctResAddPred_mmx (uint8_t* pPred, const int32_t kiStride,
                           int16_t* pRs);
#endif//X86_ASM

#if defined(__cplusplus)
}
#endif//__cplusplus

void_t GetI4LumaIChromaAddrTable (int32_t* pBlockOffset,
                                  const int32_t kiYStride, const int32_t kiUVStride);

} // namespace WelsDec

#endif//WELS_DECODE_MB_AUX_H__
