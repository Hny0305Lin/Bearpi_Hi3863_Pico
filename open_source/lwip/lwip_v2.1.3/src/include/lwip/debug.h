/**
 * @file
 * Debug messages infrastructure
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#ifndef LWIP_HDR_DEBUG_H
#define LWIP_HDR_DEBUG_H

#include "lwip/arch.h"
#include "lwip/opt.h"

/**
 * @defgroup debugging_levels LWIP_DBG_MIN_LEVEL and LWIP_DBG_TYPES_ON values
 * @ingroup lwip_opts_debugmsg
 * @{
 */

/** @name Debug level (LWIP_DBG_MIN_LEVEL)
 * @{
 */
/** Debug level: ALL messages*/
#define LWIP_DBG_LEVEL_ALL     0x00
/** Debug level: Warnings. bad checksums, dropped packets, ... */
#define LWIP_DBG_LEVEL_WARNING 0x01
/** Debug level: Serious. memory allocation failures, ... */
#define LWIP_DBG_LEVEL_SERIOUS 0x02
/** Debug level: Severe */
#define LWIP_DBG_LEVEL_SEVERE  0x03
/**
 * @}
 */

#define LWIP_DBG_MASK_LEVEL    0x03
/* compatibility define only */
#define LWIP_DBG_LEVEL_OFF     LWIP_DBG_LEVEL_ALL

/** @name Enable/disable debug messages completely (LWIP_DBG_TYPES_ON)
 * @{
 */
/** flag for LWIP_DEBUGF to enable that debug message */
#define LWIP_DBG_ON            0x80U
/** flag for LWIP_DEBUGF to disable that debug message */
#define LWIP_DBG_OFF           0x00U
/**
 * @}
 */

/** @name Debug message types (LWIP_DBG_TYPES_ON)
 * @{
 */
/** flag for LWIP_DEBUGF indicating a tracing message (to follow program flow) */
#define LWIP_DBG_TRACE         0x40U
/** flag for LWIP_DEBUGF indicating a state debug message (to follow module states) */
#define LWIP_DBG_STATE         0x20U
/** flag for LWIP_DEBUGF indicating newly added code, not thoroughly tested yet */
#define LWIP_DBG_FRESH         0x10U
/** flag for LWIP_DEBUGF to halt after printing this debug message */
#define LWIP_DBG_HALT          0x08U
/**
 * @}
 */

/**
 * @}
 */

/**
 * @defgroup lwip_assertions Assertion handling
 * @ingroup lwip_opts_debug
 * @{
 */
/**
 * LWIP_NOASSERT: Disable LWIP_ASSERT checks:
 * To disable assertions define LWIP_NOASSERT in arch/cc.h.
 */
#ifdef __DOXYGEN__
#define LWIP_NOASSERT
#undef LWIP_NOASSERT
#endif
/**
 * @}
 */

#ifndef LWIP_NOASSERT
#define LWIP_ASSERT(message, assertion) do { if (!(assertion)) { \
  LWIP_PLATFORM_ASSERT(message); }} while(0)
#else  /* LWIP_NOASSERT */
#define LWIP_ASSERT(message, assertion)
#endif /* LWIP_NOASSERT */

#ifndef LWIP_ERROR
#ifdef LWIP_DEBUG
#define LWIP_PLATFORM_ERROR(message) LWIP_PLATFORM_DIAG((message))
#else
#define LWIP_PLATFORM_ERROR(message)
#endif

/* if "expression" isn't true, then print "message" and execute "handler" expression */
#define LWIP_ERROR(message, expression, handler) do { if (!(expression)) { \
  LWIP_PLATFORM_ERROR(message); handler;}} while(0)
#endif /* LWIP_ERROR */

/** Enable debug message printing, but only if debug message type is enabled
 *  AND is of correct type AND is at least LWIP_DBG_LEVEL.
 */
#ifdef __DOXYGEN__
#define LWIP_DEBUG
#undef LWIP_DEBUG
#endif

#ifdef LWIP_DEBUG
#define LWIP_DEBUGF(debug, message) do { \
                               if ( \
                                   ((debug) & LWIP_DBG_ON) && \
                                   ((debug) & LWIP_DBG_TYPES_ON) && \
                                   ((s16_t)((debug) & LWIP_DBG_MASK_LEVEL) >= LWIP_DBG_MIN_LEVEL)) { \
                                 LWIP_PLATFORM_DIAG(message); \
                                 if ((debug) & LWIP_DBG_HALT) { \
                                   while(1); \
                                 } \
                               } \
                             } while(0)

#else  /* LWIP_DEBUG */
#define LWIP_DEBUGF(debug, message)
#endif /* LWIP_DEBUG */

#ifdef LWIP_ENABLE_DIAG_SUPPORT
#define LWIP_DEBUGF_LOG0(dbg, fmt)   diag_layer_msg_i0(0, fmt)
#define LWIP_DEBUGF_LOG1(dbg, fmt, p1)  diag_layer_msg_i1(0, fmt, (hi_u32)(p1))
#define LWIP_DEBUGF_LOG2(dbg, fmt, p1, p2)  diag_layer_msg_i2(0, fmt, (hi_u32)(p1), (hi_u32)(p2))
#define LWIP_DEBUGF_LOG3(dbg, fmt, p1, p2, p3) \
  diag_layer_msg_i3(0, fmt, (hi_u32)(p1), (hi_u32)(p2), (hi_u32)(p3))
#define LWIP_DEBUGF_LOG4(dbg, fmt, p1, p2, p3, p4) \
  diag_layer_msg_i4(0, fmt, (hi_u32)(p1), (hi_u32)(p2), (hi_u32)(p3), (hi_u32)(p4))
#else
#define LWIP_DEBUGF_LOG0(dbg, fmt)   LWIP_DEBUGF(dbg, (fmt))
#define LWIP_DEBUGF_LOG1(dbg, fmt, p1)  LWIP_DEBUGF(dbg, (fmt, p1))
#define LWIP_DEBUGF_LOG2(dbg, fmt, p1, p2)  LWIP_DEBUGF(dbg, (fmt, p1, p2))
#define LWIP_DEBUGF_LOG3(dbg, fmt, p1, p2, p3)  LWIP_DEBUGF(dbg, (fmt, p1, p2, p3))
#define LWIP_DEBUGF_LOG4(dbg, fmt, p1, p2, p3, p4) LWIP_DEBUGF(dbg, (fmt, p1, p2, p3, p4))
#endif

#ifdef LWIP_DEBUG_INFO
void debug_socket_info(int idx, int filter, int expend);
#if LWIP_TCP
struct tcp_pcb;
void debug_tcppcb_info(struct tcp_pcb *tcppcb);
#endif /* LWIP_TCP */
#if LWIP_UDP || LWIP_UDPLITE
struct udp_pcb;
void debug_udppcb_info(struct udp_pcb *udppcb);
#endif /* LWIP_UDP || LWIP_UDPLITE */
#if LWIP_RAW
struct raw_pcb;
void debug_rawpcb_info(struct raw_pcb *rawpcb);
#endif /* LWIP_RAW */
struct netconn;
void debug_netconn_info(struct netconn *conn, int expend);

struct ip_pcb;
void debug_ippcb_info(struct ip_pcb *ippcb);
void debug_memp_type_info(int type);
void debug_memp_info(void);
void debug_memp_detail(int type);
#endif /* LWIP_DEBUG_INFO */
#endif /* LWIP_HDR_DEBUG_H */
