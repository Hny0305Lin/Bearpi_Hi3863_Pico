/**
 * @file
 * SNTP client module
 */

/*
 * Copyright (c) 2007-2009 Frédéric Bernon, Simon Goldschmidt
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
 * Author: Frédéric Bernon, Simon Goldschmidt
 */


/**
 * @defgroup sntp SNTP
 * @ingroup apps
 *
 * This is simple "SNTP" client for the lwIP raw API.
 * It is a minimal implementation of SNTPv4 as specified in RFC 4330.
 *
 * For a list of some public NTP servers, see this link:
 * http://support.ntp.org/bin/view/Servers/NTPPoolServers
 *
 * @todo:
 * - complete SNTP_CHECK_RESPONSE checks 3 and 4
 */

#include "lwip/opt.h"

#include "lwip/sntp.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"

#include <string.h>
#include <time.h>

#if LWIP_SNTP

#ifndef SNTP_SUPPRESS_DELAY_CHECK
#if SNTP_UPDATE_DELAY < 15000
#error "SNTPv4 RFC 4330 enforces a minimum update time of 15 seconds (define SNTP_SUPPRESS_DELAY_CHECK to disable this error)!"
#endif
#endif

/* the various debug levels for this file */
#define SNTP_DEBUG_TRACE        (SNTP_DEBUG | LWIP_DBG_TRACE)
#define SNTP_DEBUG_STATE        (SNTP_DEBUG | LWIP_DBG_STATE)
#define SNTP_DEBUG_WARN         (SNTP_DEBUG | LWIP_DBG_LEVEL_WARNING)
#define SNTP_DEBUG_WARN_STATE   (SNTP_DEBUG | LWIP_DBG_LEVEL_WARNING | LWIP_DBG_STATE)
#define SNTP_DEBUG_SERIOUS      (SNTP_DEBUG | LWIP_DBG_LEVEL_SERIOUS)

#define SNTP_ERR_KOD                1

/* SNTP protocol defines */
#define SNTP_MSG_LEN                48

#define SNTP_OFFSET_LI_VN_MODE      0
#define SNTP_LI_MASK                0xC0
#define SNTP_LI_NO_WARNING          (0x00 << 6)
#define SNTP_LI_LAST_MINUTE_61_SEC  (0x01 << 6)
#define SNTP_LI_LAST_MINUTE_59_SEC  (0x02 << 6)
#define SNTP_LI_ALARM_CONDITION     (0x03 << 6) /* (clock not synchronized) */

#define SNTP_VERSION_MASK           0x38
#define SNTP_VERSION                (4/* NTP Version 4*/<<3)

#define SNTP_MODE_MASK              0x07
#define SNTP_MODE_CLIENT            0x03
#define SNTP_MODE_SERVER            0x04
#define SNTP_MODE_BROADCAST         0x05

#define SNTP_OFFSET_STRATUM         1
#define SNTP_STRATUM_KOD            0x00

#define SNTP_OFFSET_ORIGINATE_TIME  24
#define SNTP_OFFSET_RECEIVE_TIME    32
#define SNTP_OFFSET_TRANSMIT_TIME   40

/* Number of seconds between 1970 and Feb 7, 2036 06:28:16 UTC (epoch 1) */
#define DIFF_SEC_1970_2036          ((u32_t)2085978496L)

/** Convert NTP timestamp fraction to microseconds.
 */
#ifndef SNTP_FRAC_TO_US
# if LWIP_HAVE_INT64
#  define SNTP_FRAC_TO_US(f)        ((u32_t)(((u64_t)(f) * 1000000UL) >> 32))
# else
#  define SNTP_FRAC_TO_US(f)        ((u32_t)(f) / 4295)
# endif
#endif /* !SNTP_FRAC_TO_US */

/* Configure behaviour depending on native, microsecond or second precision.
 * Treat NTP timestamps as signed two's-complement integers. This way,
 * timestamps that have the MSB set simply become negative offsets from
 * the epoch (Feb 7, 2036 06:28:16 UTC). Representable dates range from
 * 1968 to 2104.
 */
#ifndef SNTP_SET_SYSTEM_TIME_NTP
# ifdef SNTP_SET_SYSTEM_TIME_US
#  define SNTP_SET_SYSTEM_TIME_NTP(s, f) \
    SNTP_SET_SYSTEM_TIME_US((u32_t)((s) + DIFF_SEC_1970_2036), SNTP_FRAC_TO_US(f))
# else
#  define SNTP_SET_SYSTEM_TIME_NTP(s, f) \
    SNTP_SET_SYSTEM_TIME((u32_t)((s) + DIFF_SEC_1970_2036))
# endif
#endif /* !SNTP_SET_SYSTEM_TIME_NTP */

/* Get the system time either natively as NTP timestamp or convert from
 * Unix time in seconds and microseconds. Take care to avoid overflow if the
 * microsecond value is at the maximum of 999999. Also add 0.5 us fudge to
 * avoid special values like 0, and to mask round-off errors that would
 * otherwise break round-trip conversion identity.
 */
#ifndef SNTP_GET_SYSTEM_TIME_NTP
# define SNTP_GET_SYSTEM_TIME_NTP(s, f) do { \
    u32_t sec_, usec_; \
    SNTP_GET_SYSTEM_TIME(sec_, usec_); \
    (s) = (s32_t)(sec_ - DIFF_SEC_1970_2036); \
    if (usec_ != 0) { \
      (f) = usec_ * 4295 - ((usec_ * 2143) >> 16) + 2147; \
    } else { \
      (f) = 0; \
    } \
  } while (0)
#endif /* !SNTP_GET_SYSTEM_TIME_NTP */

/* Start offset of the timestamps to extract from the SNTP packet */
#define SNTP_OFFSET_TIMESTAMPS \
    (SNTP_OFFSET_TRANSMIT_TIME + 8 - sizeof(struct sntp_timestamps))

/* Round-trip delay arithmetic helpers */
#if SNTP_COMP_ROUNDTRIP
# if !LWIP_HAVE_INT64
#  error "SNTP round-trip delay compensation requires 64-bit arithmetic"
# endif
# define SNTP_SEC_FRAC_TO_S64(s, f) \
    ((s64_t)(((u64_t)(s) << 32) | (u32_t)(f)))
# define SNTP_TIMESTAMP_TO_S64(t) \
    SNTP_SEC_FRAC_TO_S64(lwip_ntohl((t).sec), lwip_ntohl((t).frac))
#endif /* SNTP_COMP_ROUNDTRIP */

/**
 * 64-bit NTP timestamp, in network byte order.
 */
struct sntp_time {
  u32_t sec;
  u32_t frac;
};

/**
 * Timestamps to be extracted from the NTP header.
 */
struct sntp_timestamps {
#if SNTP_COMP_ROUNDTRIP || SNTP_CHECK_RESPONSE >= 2
  struct sntp_time orig;
  struct sntp_time recv;
#endif
  struct sntp_time xmit;
};

struct api_sntp_msg {
  int server_num;
  char **sntp_server;
  struct timeval *sntp_time;
  sys_sem_t cb_completed;
  err_t err;
};

/**
 * SNTP packet format (without optional fields)
 * Timestamps are coded as 64 bits:
 * - signed 32 bits seconds since Feb 07, 2036, 06:28:16 UTC (epoch 1)
 * - unsigned 32 bits seconds fraction (2^32 = 1 second)
 */
#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/bpstruct.h"
#endif
PACK_STRUCT_BEGIN
struct sntp_msg {
  PACK_STRUCT_FLD_8(u8_t  li_vn_mode);
  PACK_STRUCT_FLD_8(u8_t  stratum);
  PACK_STRUCT_FLD_8(u8_t  poll);
  PACK_STRUCT_FLD_8(u8_t  precision);
  PACK_STRUCT_FIELD(u32_t root_delay);
  PACK_STRUCT_FIELD(u32_t root_dispersion);
  PACK_STRUCT_FIELD(u32_t reference_identifier);
  PACK_STRUCT_FIELD(u32_t reference_timestamp[2]);
  PACK_STRUCT_FIELD(u32_t originate_timestamp[2]);
  PACK_STRUCT_FIELD(u32_t receive_timestamp[2]);
  PACK_STRUCT_FIELD(u32_t transmit_timestamp[2]);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END
#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/epstruct.h"
#endif

/* function prototypes */
static void sntp_request(void *arg);

/** The operating mode */
static u8_t sntp_opmode;

/** The UDP pcb used by the SNTP client */
static struct udp_pcb *sntp_pcb;
/** Names/Addresses of servers */
struct sntp_server {
#if SNTP_SERVER_DNS
  const char *name;
#endif /* SNTP_SERVER_DNS */
  ip_addr_t addr;
#if SNTP_MONITOR_SERVER_REACHABILITY
  /** Reachability shift register as described in RFC 5905 */
  u8_t reachability;
#endif /* SNTP_MONITOR_SERVER_REACHABILITY */
};
static struct sntp_server sntp_servers[SNTP_MAX_SERVERS];

#if SNTP_GET_SERVERS_FROM_DHCP || SNTP_GET_SERVERS_FROM_DHCPV6
static u8_t sntp_set_servers_from_dhcp;
#endif /* SNTP_GET_SERVERS_FROM_DHCP || SNTP_GET_SERVERS_FROM_DHCPV6 */
/** The currently used server (initialized to 0) */
static u8_t sntp_current_server;
static u8_t sntp_kod_found;
static int sntp_num_servers;

static struct api_sntp_msg smsg;

#if SNTP_RETRY_TIMEOUT_EXP
#define SNTP_RESET_RETRY_TIMEOUT() sntp_retry_timeout = SNTP_RETRY_TIMEOUT

static u8_t sntp_retry_count;

/** Retry time, initialized with SNTP_RETRY_TIMEOUT and doubled with each retry. */
static u32_t sntp_retry_timeout;
#else /* SNTP_RETRY_TIMEOUT_EXP */
#define SNTP_RESET_RETRY_TIMEOUT()
#define sntp_retry_timeout SNTP_RETRY_TIMEOUT
#endif /* SNTP_RETRY_TIMEOUT_EXP */

#if SNTP_CHECK_RESPONSE >= 1
/** Saves the last server address to compare with response */
static ip_addr_t sntp_last_server_address;
#endif /* SNTP_CHECK_RESPONSE >= 1 */

#if SNTP_CHECK_RESPONSE >= 2
/** Saves the last timestamp sent (which is sent back by the server)
 * to compare against in response. Stored in network byte order. */
static struct sntp_time sntp_last_timestamp_sent;
#endif /* SNTP_CHECK_RESPONSE >= 2 */

#if defined(LWIP_DEBUG) && !defined(sntp_format_time)
/* Debug print helper. */
static const char *
sntp_format_time(s32_t sec)
{
  time_t ut;
  ut = (u32_t)((u32_t)sec + DIFF_SEC_1970_2036);
  return ctime(&ut);
}
#endif /* LWIP_DEBUG && !sntp_format_time */

/**
 * SNTP processing of received timestamp
 */
static void
sntp_process(const struct sntp_timestamps *timestamps)
{
  s32_t sec;
  u32_t frac;

  sec  = (s32_t)lwip_ntohl(timestamps->xmit.sec);
  frac = lwip_ntohl(timestamps->xmit.frac);

#if SNTP_COMP_ROUNDTRIP
# if SNTP_CHECK_RESPONSE >= 2
  if (timestamps->recv.sec != 0 || timestamps->recv.frac != 0)
# endif
  {
    s32_t dest_sec;
    u32_t dest_frac;
    u32_t step_sec;

    /* Get the destination time stamp, i.e. the current system time */
    SNTP_GET_SYSTEM_TIME_NTP(dest_sec, dest_frac);

    step_sec = (dest_sec < sec) ? ((u32_t)sec - (u32_t)dest_sec)
               : ((u32_t)dest_sec - (u32_t)sec);
    /* In order to avoid overflows, skip the compensation if the clock step
     * is larger than about 34 years. */
    if ((step_sec >> 30) == 0) {
      s64_t t1, t2, t3, t4;

      t4 = SNTP_SEC_FRAC_TO_S64(dest_sec, dest_frac);
      t3 = SNTP_SEC_FRAC_TO_S64(sec, frac);
      t1 = SNTP_TIMESTAMP_TO_S64(timestamps->orig);
      t2 = SNTP_TIMESTAMP_TO_S64(timestamps->recv);
      /* Clock offset calculation according to RFC 4330 */
      t4 += ((t2 - t1) + (t3 - t4)) / 2;

      sec  = (s32_t)((u64_t)t4 >> 32);
      frac = (u32_t)((u64_t)t4);
    }
  }
#endif /* SNTP_COMP_ROUNDTRIP */

  SNTP_SET_SYSTEM_TIME_NTP(sec, frac);
  if (smsg.sntp_time != NULL) {
    smsg.sntp_time->tv_sec = (long)sec;
    smsg.sntp_time->tv_usec = (long)frac;
  }
  LWIP_UNUSED_ARG(frac); /* might be unused if only seconds are set */
  LWIP_DEBUGF(SNTP_DEBUG_TRACE, ("sntp_process: %s, %" U32_F " us\n",
                                 sntp_format_time(sec), SNTP_FRAC_TO_US(frac)));
}

/**
 * Initialize request struct to be sent to server.
 */
static void
sntp_initialize_request(struct sntp_msg *req)
{
  memset(req, 0, SNTP_MSG_LEN);
  req->li_vn_mode = SNTP_LI_NO_WARNING | SNTP_VERSION | SNTP_MODE_CLIENT;

#if SNTP_CHECK_RESPONSE >= 2 || SNTP_COMP_ROUNDTRIP
  {
    s32_t secs;
    u32_t sec, frac;
    /* Get the transmit timestamp */
    SNTP_GET_SYSTEM_TIME_NTP(secs, frac);
    sec  = lwip_htonl((u32_t)secs);
    frac = lwip_htonl(frac);

# if SNTP_CHECK_RESPONSE >= 2
    sntp_last_timestamp_sent.sec  = sec;
    sntp_last_timestamp_sent.frac = frac;
# endif
    req->transmit_timestamp[0] = sec;
    req->transmit_timestamp[1] = frac;
  }
#endif /* SNTP_CHECK_RESPONSE >= 2 || SNTP_COMP_ROUNDTRIP */
}

/**
 * Retry: send a new request (and increase retry timeout).
 *
 * @param arg is unused (only necessary to conform to sys_timeout)
 */
static void
sntp_retry(void *arg)
{
  LWIP_UNUSED_ARG(arg);

  LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_retry: Next request will be sent in %"U32_F" ms\n",
                                 sntp_retry_timeout));

  /* set up a timer to send a retry and increase the retry delay */
  sys_untimeout(sntp_request, NULL);
  if (sys_timeout(sntp_retry_timeout, sntp_request, NULL) != ERR_OK) {
    smsg.err = ERR_MEM;
    sys_sem_signal(&smsg.cb_completed);
    return;
  }

#if SNTP_RETRY_TIMEOUT_EXP
  {
    u32_t new_retry_timeout;
    /* increase the timeout for next retry */
    new_retry_timeout = sntp_retry_timeout << 1;
    /* limit to maximum timeout and prevent overflow */
    if ((new_retry_timeout <= SNTP_RETRY_TIMEOUT_MAX) &&
        (new_retry_timeout > sntp_retry_timeout)) {
      sntp_retry_timeout = new_retry_timeout;
    } else {
      sntp_retry_timeout = SNTP_RETRY_TIMEOUT_MAX;
    }
  }
#endif /* SNTP_RETRY_TIMEOUT_EXP */
}

/**
 * If Kiss-of-Death is received (or another packet parsing error),
 * try the next server or retry the current server and increase the retry
 * timeout if only one server is available.
 * (implicitly, SNTP_MAX_SERVERS > 1)
 *
 * @param arg is unused (only necessary to conform to sys_timeout)
 */
static void
sntp_try_next_server(void *arg)
{
  LWIP_UNUSED_ARG(arg);

  /* sntp_pcb has been released, that implies the response came too late... just ignore it */
  if (sntp_pcb == NULL) {
    return;
  }

  sntp_current_server++;
  if (sntp_current_server >= sntp_num_servers) {
    /* Stop sntp module */
    sntp_stop();
    if (sntp_kod_found) {
      smsg.err = ERR_ACCESS;
    } else {
      smsg.err = ERR_TIMEOUT;
    }
    sys_sem_signal(&smsg.cb_completed);
    return;
  }

  /* reset KoD found flag for another SNTP server */
  sntp_kod_found = 0;

  LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_try_next_server: Sending request to server %"U16_F"\n",
                                 (u16_t)sntp_current_server));
  /* new server: reset retry timeout */
  SNTP_RESET_RETRY_TIMEOUT();
  /* instantly send a request to the next server */
  sntp_request(NULL);
}

/** UDP recv callback for the sntp pcb */
static void
sntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  struct sntp_timestamps timestamps;
  u8_t mode;
  u8_t stratum;
  err_t err;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(pcb);

  /* packet received: stop retry timeout */
  sys_untimeout(sntp_try_next_server, NULL);
  sys_untimeout(sntp_request, NULL);

  err = ERR_ARG;
#if SNTP_CHECK_RESPONSE >= 1
  /* check server address and port */
  if (((sntp_opmode != SNTP_OPMODE_POLL) || ip_addr_cmp(addr, &sntp_last_server_address)) &&
      (port == SNTP_PORT))
#else /* SNTP_CHECK_RESPONSE >= 1 */
  LWIP_UNUSED_ARG(addr);
  LWIP_UNUSED_ARG(port);
#endif /* SNTP_CHECK_RESPONSE >= 1 */
  {
    /* process the response */
    if (p->tot_len == SNTP_MSG_LEN) {
      mode = pbuf_get_at(p, SNTP_OFFSET_LI_VN_MODE) & SNTP_MODE_MASK;
      /* if this is a SNTP response... */
      if (((sntp_opmode == SNTP_OPMODE_POLL)       && (mode == SNTP_MODE_SERVER)) ||
          ((sntp_opmode == SNTP_OPMODE_LISTENONLY) && (mode == SNTP_MODE_BROADCAST))) {
        stratum = pbuf_get_at(p, SNTP_OFFSET_STRATUM);

        if (stratum == SNTP_STRATUM_KOD) {
          /* Kiss-of-death packet. Use another server or increase UPDATE_DELAY. */
          err = SNTP_ERR_KOD;
          sntp_kod_found = 1;
          LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_recv: Received Kiss-of-Death\n"));
        } else {
          pbuf_copy_partial(p, &timestamps, sizeof(timestamps), SNTP_OFFSET_TIMESTAMPS);
#if SNTP_CHECK_RESPONSE >= 2
          /* check originate_timetamp against sntp_last_timestamp_sent */
          if (timestamps.orig.sec != sntp_last_timestamp_sent.sec ||
              timestamps.orig.frac != sntp_last_timestamp_sent.frac) {
            LWIP_DEBUGF(SNTP_DEBUG_WARN,
                        ("sntp_recv: Invalid originate timestamp in response\n"));
          } else
#endif /* SNTP_CHECK_RESPONSE >= 2 */
            /* @todo: add code for SNTP_CHECK_RESPONSE >= 3 and >= 4 here */
          {
            /* correct answer */
            err = ERR_OK;
          }
        }
      } else {
        LWIP_DEBUGF(SNTP_DEBUG_WARN, ("sntp_recv: Invalid mode in response: %"U16_F"\n", (u16_t)mode));
        /* wait for correct response */
        err = ERR_TIMEOUT;
      }
    } else {
      LWIP_DEBUGF(SNTP_DEBUG_WARN, ("sntp_recv: Invalid packet length: %"U16_F"\n", p->tot_len));
    }
  }
#if SNTP_CHECK_RESPONSE >= 1
  else {
    /* packet from wrong remote address or port, wait for correct response */
    err = ERR_TIMEOUT;
  }
#endif /* SNTP_CHECK_RESPONSE >= 1 */

  pbuf_free(p);

  if (err == ERR_OK) {
    /* correct packet received: process it it */
    sntp_process(&timestamps);

#if SNTP_MONITOR_SERVER_REACHABILITY
    /* indicate that server responded */
    sntp_servers[sntp_current_server].reachability |= 1;
#endif /* SNTP_MONITOR_SERVER_REACHABILITY */
    /* Stop sntp module */
    sntp_stop();

    smsg.err = (err_t)sntp_current_server;

    /* Return success */
    sys_sem_signal(&smsg.cb_completed);

  } else if (err == SNTP_ERR_KOD) {
    /* KOD errors are only processed in case of an explicit poll response */
    if (sntp_opmode == SNTP_OPMODE_POLL) {
      /* Kiss-of-death packet. Use another server or increase UPDATE_DELAY. */
      sntp_try_next_server(NULL);
    }
  } else {
    /* broken packet: try the same server again, limit max retry count to avoid flooding */
    sntp_retry(NULL);
  }
}

/** Actually send an sntp request to a server.
 *
 * @param server_addr resolved IP address of the SNTP server
 */
static void
sntp_send_request(const ip_addr_t *server_addr)
{
  struct pbuf *p;

  LWIP_ASSERT("server_addr != NULL", server_addr != NULL);

  /* sntp_pcb has been released, that implies the response came too late... just ignore it */
  if (sntp_pcb == NULL) {
    return;
  }

  p = pbuf_alloc(PBUF_TRANSPORT, SNTP_MSG_LEN, PBUF_RAM);
  if (p != NULL) {
    struct sntp_msg *sntpmsg = (struct sntp_msg *)p->payload;
    LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_send_request: Sending request to server\n"));
    /* initialize request message */
    sntp_initialize_request(sntpmsg);
    /* send request */
    udp_sendto(sntp_pcb, p, server_addr, SNTP_PORT);
    /* free the pbuf after sending it */
    pbuf_free(p);
#if SNTP_MONITOR_SERVER_REACHABILITY
    /* indicate new packet has been sent */
    sntp_servers[sntp_current_server].reachability <<= 1;
#endif /* SNTP_MONITOR_SERVER_REACHABILITY */
    /* set up receive timeout: try next server or retry on timeout */
    if (sntp_retry_count >= SNTP_MAX_REQUEST_RETRANSMIT) {
      sntp_retry_count = 0;
      LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_retry: Sending request to next server\n"));
      sys_untimeout(sntp_try_next_server, NULL);
      if (sys_timeout((u32_t)SNTP_RECV_TIMEOUT, sntp_try_next_server, NULL) != ERR_OK) {
        smsg.err = ERR_MEM;
        sys_sem_signal(&smsg.cb_completed);
        return;
      }
    } else {
      sntp_retry_count++;
      LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_retry: Sending request to server\n"));
      sntp_retry(NULL);
    }
#if SNTP_CHECK_RESPONSE >= 1
    /* save server address to verify it in sntp_recv */
    ip_addr_copy(sntp_last_server_address, *server_addr);
#endif /* SNTP_CHECK_RESPONSE >= 1 */
  } else {
    LWIP_DEBUGF(SNTP_DEBUG_SERIOUS, ("sntp_send_request: Out of memory, trying again in %"U32_F" ms\n",
                                     (u32_t)SNTP_RETRY_TIMEOUT));
    /* out of memory: set up a timer to send a retry */
    sys_untimeout(sntp_request, NULL);
    if (sys_timeout((u32_t)SNTP_RETRY_TIMEOUT, sntp_request, NULL) != ERR_OK) {
      smsg.err = ERR_MEM;
      sys_sem_signal(&smsg.cb_completed);
    }
  }
}

#if SNTP_SERVER_DNS
/**
 * DNS found callback when using DNS names as server address.
 */
static void
sntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, u32_t count, void *arg)
{
  LWIP_UNUSED_ARG(hostname);
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(count);

  /* sntp_pcb has been released, that implies the response came too late... just ignore it */
  if (sntp_pcb == NULL) {
    return;
  }

  if (ipaddr != NULL) {
    /* Address resolved, send request */
    LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_dns_found: Server address resolved, sending request\n"));
    sntp_servers[sntp_current_server].addr = *ipaddr;
    sntp_send_request(ipaddr);
  } else {
    /* DNS resolving failed -> try another server */
    LWIP_DEBUGF(SNTP_DEBUG_WARN_STATE, ("sntp_dns_found: Failed to resolve server address resolved, trying next server\n"));
    sntp_try_next_server(NULL);
  }
}
#endif /* SNTP_SERVER_DNS */

/**
 * Send out an sntp request.
 *
 * @param arg is unused (only necessary to conform to sys_timeout)
 */
static void
sntp_request(void *arg)
{
  ip_addr_t sntp_server_address;
  err_t err;

#if SNTP_SERVER_DNS
  u32_t count = 1;
#endif /* SNTP_SERVER_DNS */

  LWIP_UNUSED_ARG(arg);

  /* initialize SNTP server address */
#if SNTP_SERVER_DNS
  if (sntp_servers[sntp_current_server].name) {
    /* always resolve the name and rely on dns-internal caching & timeout */
    ip_addr_set_zero(&sntp_servers[sntp_current_server].addr);
    err = dns_gethostbyname(sntp_servers[sntp_current_server].name, &sntp_server_address, &count,
                            sntp_dns_found, NULL);
    if (err == ERR_INPROGRESS) {
      /* DNS request sent, wait for sntp_dns_found being called */
      LWIP_DEBUGF(SNTP_DEBUG_STATE, ("sntp_request: Waiting for server address to be resolved.\n"));
      return;
    } else if (err == ERR_OK) {
      sntp_servers[sntp_current_server].addr = sntp_server_address;
    }
  } else
#endif /* SNTP_SERVER_DNS */
  {
    sntp_server_address = sntp_servers[sntp_current_server].addr;
    err = (ip_addr_isany_val(sntp_server_address)) ? ERR_ARG : ERR_OK;
  }

  if (err == ERR_OK) {
#ifdef LWIP_DEBUG
    char buf[IPADDR_STRLEN_MAX];
    (void)ipaddr_ntoa_r(&sntp_server_address, buf, IPADDR_STRLEN_MAX);
#endif
    LWIP_DEBUGF(SNTP_DEBUG_TRACE, ("sntp_request: current server address is %s\n",
                                   buf));
    sntp_send_request(&sntp_server_address);
  } else {
    /* address conversion failed, try another server */
    LWIP_DEBUGF(SNTP_DEBUG_WARN_STATE, ("sntp_request: Invalid server address, trying next server.\n"));
    sys_untimeout(sntp_try_next_server, NULL);
    if (sys_timeout((u32_t)SNTP_RETRY_TIMEOUT, sntp_try_next_server, NULL) != ERR_OK) {
      smsg.err = ERR_MEM;
      sys_sem_signal(&smsg.cb_completed);
    }
  }
}

/**
 * @ingroup sntp
 * Initialize this module.
 * Send out request instantly or after SNTP_STARTUP_DELAY(_FUNC).
 */
void
sntp_init(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  /* LWIP_ASSERT_CORE_LOCKED(); is checked by udp_new() */
  LWIP_DEBUGF(SNTP_DEBUG_TRACE, ("sntp_init: SNTP initialised\n"));

#ifdef SNTP_SERVER_ADDRESS
#if SNTP_SERVER_DNS
  sntp_setservername(0, SNTP_SERVER_ADDRESS);
#else
#error SNTP_SERVER_ADDRESS string not supported SNTP_SERVER_DNS==0
#endif
#endif /* SNTP_SERVER_ADDRESS */

  if (sntp_pcb == NULL) {
    int server_num = 0;
    int i;
    sntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    LWIP_ASSERT("Failed to allocate udp pcb for sntp client", sntp_pcb != NULL);
    if (sntp_pcb != NULL) {
      for (i = 0; i < LWIP_MIN(smsg.server_num, SNTP_MAX_SERVERS); i++) {
#if SNTP_SERVER_DNS
        sntp_setservername((u8_t)server_num, smsg.sntp_server[i]);
        server_num++;
#else /* SNTP_SERVER_DNS */
        ip_addr_t server_ip;
        if (ipaddr_aton(smsg.sntp_server[i], &server_ip) == 1) {
          sntp_setserver(server_num, &server_ip);
          server_num++;
        }
#endif /* SNTP_SERVER_DNS */
      }

      if (server_num <= 0) {
        sntp_stop();
        smsg.err = ERR_ARG;
        sys_sem_signal(&smsg.cb_completed);
      }
      sntp_current_server = 0;
      sntp_num_servers = server_num;
      sntp_retry_count = 0;
      sntp_kod_found = 0;
      udp_recv(sntp_pcb, sntp_recv, NULL);

      if (sntp_opmode == SNTP_OPMODE_POLL) {
        SNTP_RESET_RETRY_TIMEOUT();
#if LWIP_SO_PRIORITY
      sntp_pcb->priority = LWIP_PKT_PRIORITY_SNTP;
#endif /* LWIP_SO_PRIORITY */
#if SNTP_STARTUP_DELAY
        if (sys_timeout((u32_t)SNTP_STARTUP_DELAY_FUNC, sntp_request, NULL) != ERR_OK) {
          smsg.err = ERR_MEM;
          sys_sem_signal(&smsg.cb_completed);
        }
#else
        sntp_request(NULL);
#endif
      } else if (sntp_opmode == SNTP_OPMODE_LISTENONLY) {
        ip_set_option(sntp_pcb, SOF_BROADCAST);
        udp_bind(sntp_pcb, IP_ANY_TYPE, SNTP_PORT);
      }
    } else {
      smsg.err = ERR_MEM;
      sys_sem_signal(&smsg.cb_completed);
    }
  } else {
    LWIP_DEBUGF(SNTP_DEBUG_WARN, ("sntp_init: In processing, please try later\n"));
    smsg.err = ERR_INPROGRESS;
    sys_sem_signal(&smsg.cb_completed);
  }
}

int
lwip_sntp_start(int server_num, char **sntp_server, struct timeval *time_local)
{
  err_t err;

  if ((server_num == 0) || (sntp_server == NULL) || (server_num > DNS_MAX_SERVERS)) {
    return ERR_ARG;
  }

  err = sys_sem_new(&smsg.cb_completed, 0);
  if (err != ERR_OK) {
    return ERR_MEM;
  }

  smsg.server_num = server_num;
  smsg.sntp_server = sntp_server;
  smsg.sntp_time = time_local;
  smsg.err = ERR_OK;
  err = tcpip_callback(sntp_init, NULL);
  if (err != ERR_OK) {
    sys_sem_free(&smsg.cb_completed);
    return err;
  }
  err = (err_t)sys_arch_sem_wait(&smsg.cb_completed, 0);
  if ((err == (err_t)SYS_ARCH_ERROR) || (err == (err_t)SYS_ARCH_TIMEOUT)) {
    sys_sem_free(&smsg.cb_completed);
    return err;
  }
  sys_sem_free(&smsg.cb_completed);
  return smsg.err;
}

/**
 * @ingroup sntp
 * Stop this module.
 */
void
sntp_stop(void)
{
  LWIP_ASSERT_CORE_LOCKED();
  if (sntp_pcb != NULL) {
#if SNTP_MONITOR_SERVER_REACHABILITY
    u8_t i;
    for (i = 0; i < SNTP_MAX_SERVERS; i++) {
      sntp_servers[i].reachability = 0;
    }
#endif /* SNTP_MONITOR_SERVER_REACHABILITY */
    sys_untimeout(sntp_request, NULL);
    sys_untimeout(sntp_try_next_server, NULL);
    udp_remove(sntp_pcb);
    sntp_pcb = NULL;
  }
}

/**
 * @ingroup sntp
 * Get enabled state.
 */
u8_t sntp_enabled(void)
{
  return (sntp_pcb != NULL) ? 1 : 0;
}

/**
 * @ingroup sntp
 * Sets the operating mode.
 * @param operating_mode one of the available operating modes
 */
void
sntp_setoperatingmode(u8_t operating_mode)
{
  LWIP_ASSERT_CORE_LOCKED();
  LWIP_ASSERT("Invalid operating mode", operating_mode <= SNTP_OPMODE_LISTENONLY);
  LWIP_ASSERT("Operating mode must not be set while SNTP client is running", sntp_pcb == NULL);
  sntp_opmode = operating_mode;
}

/**
 * @ingroup sntp
 * Gets the operating mode.
 */
u8_t
sntp_getoperatingmode(void)
{
  return sntp_opmode;
}

#if SNTP_MONITOR_SERVER_REACHABILITY
/**
 * @ingroup sntp
 * Gets the server reachability shift register as described in RFC 5905.
 *
 * @param idx the index of the NTP server
 */
u8_t
sntp_getreachability(u8_t idx)
{
  if (idx < SNTP_MAX_SERVERS) {
    return sntp_servers[idx].reachability;
  }
  return 0;
}
#endif /* SNTP_MONITOR_SERVER_REACHABILITY */

#if SNTP_GET_SERVERS_FROM_DHCP || SNTP_GET_SERVERS_FROM_DHCPV6
/**
 * Config SNTP server handling by IP address, name, or DHCP; clear table
 * @param set_servers_from_dhcp enable or disable getting server addresses from dhcp
 */
void
sntp_servermode_dhcp(int set_servers_from_dhcp)
{
  u8_t new_mode = set_servers_from_dhcp ? 1 : 0;
  LWIP_ASSERT_CORE_LOCKED();
  if (sntp_set_servers_from_dhcp != new_mode) {
    sntp_set_servers_from_dhcp = new_mode;
  }
}
#endif /* SNTP_GET_SERVERS_FROM_DHCP || SNTP_GET_SERVERS_FROM_DHCPV6 */

/**
 * @ingroup sntp
 * Initialize one of the NTP servers by IP address
 *
 * @param idx the index of the NTP server to set must be < SNTP_MAX_SERVERS
 * @param server IP address of the NTP server to set
 */
void
sntp_setserver(u8_t idx, const ip_addr_t *server)
{
  LWIP_ASSERT_CORE_LOCKED();
  if (idx < SNTP_MAX_SERVERS) {
    if (server != NULL) {
      sntp_servers[idx].addr = (*server);
    } else {
      ip_addr_set_zero(&sntp_servers[idx].addr);
    }
#if SNTP_SERVER_DNS
    sntp_servers[idx].name = NULL;
#endif
  }
}

#if LWIP_DHCP && SNTP_GET_SERVERS_FROM_DHCP
/**
 * Initialize one of the NTP servers by IP address, required by DHCP
 *
 * @param num the index of the NTP server to set must be < SNTP_MAX_SERVERS
 * @param server IP address of the NTP server to set
 */
void
dhcp_set_ntp_servers(u8_t num, const ip4_addr_t *server)
{
  LWIP_DEBUGF(SNTP_DEBUG_TRACE, ("sntp: %s %u.%u.%u.%u as NTP server #%u via DHCP\n",
                                 (sntp_set_servers_from_dhcp ? "Got" : "Rejected"),
                                 ip4_addr1(server), ip4_addr2(server), ip4_addr3(server), ip4_addr4(server), num));
  if (sntp_set_servers_from_dhcp && num) {
    u8_t i;
    for (i = 0; (i < num) && (i < SNTP_MAX_SERVERS); i++) {
      ip_addr_t addr;
      ip_addr_copy_from_ip4(addr, server[i]);
      sntp_setserver(i, &addr);
    }
    for (i = num; i < SNTP_MAX_SERVERS; i++) {
      sntp_setserver(i, NULL);
    }
  }
}
#endif /* LWIP_DHCP && SNTP_GET_SERVERS_FROM_DHCP */

#if LWIP_IPV6_DHCP6 && SNTP_GET_SERVERS_FROM_DHCPV6
/**
 * Initialize one of the NTP servers by IP address, required by DHCPV6
 *
 * @param num the number of NTP server addresses to set must be < SNTP_MAX_SERVERS
 * @param server array of IP address of the NTP servers to set
 */
void
dhcp6_set_ntp_servers(u8_t num_ntp_servers, ip_addr_t* ntp_server_addrs)
{
  LWIP_DEBUGF(SNTP_DEBUG_TRACE, ("sntp: %s %u NTP server(s) via DHCPv6\n",
                                 (sntp_set_servers_from_dhcp ? "Got" : "Rejected"),
                                 num_ntp_servers));
  if (sntp_set_servers_from_dhcp && num_ntp_servers) {
    u8_t i;
    for (i = 0; (i < num_ntp_servers) && (i < SNTP_MAX_SERVERS); i++) {
      LWIP_DEBUGF(SNTP_DEBUG_TRACE, ("sntp: NTP server %u: %s\n",
                                     i, ipaddr_ntoa(&ntp_server_addrs[i])));
      sntp_setserver(i, &ntp_server_addrs[i]);
    }
    for (i = num_ntp_servers; i < SNTP_MAX_SERVERS; i++) {
      sntp_setserver(i, NULL);
    }
  }
}
#endif /* LWIP_DHCPv6 && SNTP_GET_SERVERS_FROM_DHCPV6 */

/**
 * @ingroup sntp
 * Obtain one of the currently configured by IP address (or DHCP) NTP servers
 *
 * @param idx the index of the NTP server
 * @return IP address of the indexed NTP server or "ip_addr_any" if the NTP
 *         server has not been configured by address (or at all).
 */
const ip_addr_t *
sntp_getserver(u8_t idx)
{
  if (idx < SNTP_MAX_SERVERS) {
    return &sntp_servers[idx].addr;
  }
  return IP_ADDR_ANY;
}

#if SNTP_SERVER_DNS
/**
 * Initialize one of the NTP servers by name
 *
 * @param idx the index of the NTP server to set must be < SNTP_MAX_SERVERS
 * @param server DNS name of the NTP server to set, to be resolved at contact time
 */
void
sntp_setservername(u8_t idx, const char *server)
{
  LWIP_ASSERT_CORE_LOCKED();
  if (idx < SNTP_MAX_SERVERS) {
    sntp_servers[idx].name = server;
  }
}

/**
 * Obtain one of the currently configured by name NTP servers.
 *
 * @param idx the index of the NTP server
 * @return IP address of the indexed NTP server or NULL if the NTP
 *         server has not been configured by name (or at all)
 */
const char *
sntp_getservername(u8_t idx)
{
  if (idx < SNTP_MAX_SERVERS) {
    return sntp_servers[idx].name;
  }
  return NULL;
}
#endif /* SNTP_SERVER_DNS */

#endif /* LWIP_SNTP */
