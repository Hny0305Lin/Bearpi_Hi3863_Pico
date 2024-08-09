/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2013-2020. All rights reserved.
 * Description: shell cmds APIs implementation about ping and ping6
 * Author: none
 * Create: 2020
 */

#include "lwip/nettool/ping.h"
#include "lwip/icmp.h"
#include "lwip/icmp6.h"
#include "lwip/sockets.h"
#include "lwip/ip.h"
#include "lwip/inet_chksum.h"
#include "lwip/nettool/utility.h"
#include "lwip/netdb.h"
#include "netif/ifaddrs.h"
#include "lwip/sys.h"
#if LWIP_LITEOS_COMPAT
#include "los_config.h"
#include <td_base.h>
#endif


#if LWIP_ENABLE_BASIC_SHELL_CMD

/** LWIP_SHELL_CMD_PING_TIMEOUT: Ping cmd waiting timeout max(in millisec) to receive ping response */
#if !defined LWIP_SHELL_CMD_PING_TIMEOUT_MAX || defined __DOXYGEN__
#define LWIP_SHELL_CMD_PING_TIMEOUT_MAX 10000
#endif

/** LWIP_SHELL_CMD_PING_TIMEOUT: Ping cmd waiting timeout min(in millisec) to receive ping response */
#if !defined LWIP_SHELL_CMD_PING_TIMEOUT_MIN || defined __DOXYGEN__
#define LWIP_SHELL_CMD_PING_TIMEOUT_MIN 1000
#endif

#ifdef CUSTOM_AT_COMMAND
#define LWIP_PING_TASK_PRIO 5
#define LWIP_PING_MAX_PKT_LEN 7360
#else
#define LWIP_PING_TASK_PRIO 4
#endif

#define LWIP_PING_INTERFACE_ARG       1
#define LWIP_PING_HOSTNAME_ARG        2
#define LWIP_PING_DEFAULT_SOCKET      4
#define PERCENT 100

/* Maximum tick value that can be generated by sys_now() */
#define LWIP_MAX_TICK                 0xFFFFFFFF
#define LWIP_MAX_TICK_U64             0xFFFFFFFFFFFFFFFFLLU
#define LWIP_DEFAULT_PING_TASK_ID     0xFFFFFFFF

static uint32_t ping_taskid = LWIP_DEFAULT_PING_TASK_ID;
static int ping_kill = 0;
static u8_t ping_task_running = 0;
#define PING_ZERO_DATA_LEN 8
#define SEC_TO_US   1000000
#define US_TO_NSEC  1000

static void
lwip_ping_usage(u32_t is_v6)
{
#ifndef CUSTOM_AT_COMMAND
  char *ping = is_v6 ? "ping6" : "ping";
  LWIP_PLATFORM_PRINT("Usage:"\
         CRLF"  %s"
         CRLF"  %s [-6] [-n cnt] [-w interval] [-l len] dest"
         CRLF"  %s [-I iface/ipaddr] [-t] [-w interval] [-W timeout] dest"
         CRLF"  %s -k  use -k to stop"
         CRLF"  %s -t  ping forever"CRLF, ping, ping, ping, ping, ping);
#else
  (void)is_v6;
#endif
}

#if LWIP_LITEOS_TASK
/* help convert ptr to u32 array(if 64bit platfrom) */
union los_ptr_args {
  void *ptr;
  u32_t args[2];
};
#endif

struct ping_run_ctx {
  int sfd;
  u32_t is_v6;
  struct sockaddr_storage to;
  struct icmp_echo_hdr *iecho;
  struct icmp_echo_hdr *iecho_resp;
  struct ip_hdr *iphdr_resp;
  u32_t iecho_len;
  s16_t ip_hlen;
  u16_t i;
  u32_t forever;
  u32_t succ_cnt;
  u32_t failed_cnt;
#if LWIP_LITEOS_TASK
  u64_t start_us;
  u64_t end_us;
  u64_t timout_end_us;
#else
  u32_t start_ms;
  u32_t end_ms;
  u32_t timout_end_ms;
#endif

#ifdef CUSTOM_AT_COMMAND
  s32_t rtt_sum;
  s32_t rtt_min;
  s32_t rtt_max;
#endif

  s32_t timout_ms;
#if LWIP_SOCKET_POLL && !LWIP_EXT_POLL_SUPPORT
  struct pollfd pfd;
#else
  fd_set read_set;
  struct timeval time_val;
#endif
  s32_t rtt;
  u32_t intrvl;
  char *data_buf;
  struct sockaddr_storage dst;
  u32_t socklen;
  u32_t cnt;
  u32_t timout;
  u32_t interval;
  u32_t data_len;
};

#define PING_ADDR_STR_LEN IPADDR_STRLEN_MAX

struct ping_cfg {
  u32_t count;
  u32_t interval;
  u32_t timeout;
  u32_t data_len;
  struct sockaddr_storage dst;
  u8_t src_type;
  u32_t is_v6;
  char src_iface[NETIF_NAMESIZE];
  struct sockaddr_storage src;
};

#ifdef CONFIG_SIGMA_SUPPORT
ping_result_callback upload_ping_result = NULL;
void ping_add_ext_callback(ping_result_callback func)
{
  upload_ping_result = func;
}
#endif

LWIP_STATIC void
ping_req_init(struct ping_run_ctx *ctx)
{
  u32_t i;
  if (ctx->data_len > PING_ZERO_DATA_LEN) {
    (void)memset_s(ctx->iecho, sizeof(struct icmp_echo_hdr) + PING_ZERO_DATA_LEN, 0,
                   sizeof(struct icmp_echo_hdr) + PING_ZERO_DATA_LEN);
    ctx->data_buf = (char *)ctx->iecho + sizeof(struct icmp_echo_hdr) + PING_ZERO_DATA_LEN;
    for (i = 0; i < ctx->data_len - PING_ZERO_DATA_LEN; i++) {
      *(ctx->data_buf + i) = i + 0x10;
    }
  } else {
    (void)memset_s(ctx->iecho, sizeof(struct icmp_echo_hdr) + ctx->data_len, 0,
                   sizeof(struct icmp_echo_hdr) + ctx->data_len);
  }
  ctx->iecho->id = LWIP_RAND();

#if LWIP_IPV6
  if (ctx->is_v6) {
    ICMPH_TYPE_SET(ctx->iecho, (u8_t)ICMP6_TYPE_EREQ);
  } else
#endif
  {
    ICMPH_TYPE_SET(ctx->iecho, (u8_t)ICMP_ECHO);
  }
}

LWIP_STATIC int
ping_ctx_init(struct ping_run_ctx *ctx, struct ping_cfg *cfg)
{
  ctx->sfd = -1;
  ctx->is_v6 = cfg->is_v6;
  ctx->iecho = NULL;
  ctx->iecho_resp = NULL;
  ctx->iphdr_resp = NULL;
  ctx->succ_cnt = 0;
  ctx->failed_cnt = 0;

#ifdef CUSTOM_AT_COMMAND
  ctx->rtt_sum = 0;
  ctx->rtt_min = 0;
  ctx->rtt_max = 0;
#endif
  ctx->data_buf = NULL;
  ctx->dst = cfg->dst;
#if LWIP_IPV6
  ctx->socklen = (ctx->dst.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
#else
  ctx->socklen = sizeof(struct sockaddr_in);
#endif
  ctx->cnt = cfg->count;
  ctx->interval = cfg->interval;
  ctx->timout = cfg->timeout;
  ctx->data_len = cfg->data_len;

  ctx->iecho_len = sizeof(struct icmp_echo_hdr) + ctx->data_len;
  ctx->iphdr_resp = (struct ip_hdr *)mem_malloc(ctx->iecho_len + IP_HLEN_MAX);
  if (ctx->iphdr_resp == NULL) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Ping: malloc fail"CRLF);
#else
    LWIP_PLATFORM_PRINT("Ping: malloc fail"CRLF);
#endif
    return -1;
  }
  ctx->iecho = (struct icmp_echo_hdr *)mem_malloc(ctx->iecho_len);
  if (ctx->iecho == NULL) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Ping: request malloc fail"CRLF);
#else
    LWIP_PLATFORM_PRINT("Ping: request malloc fail"CRLF);
#endif
    return -1;
  }
  ctx->to = cfg->dst;
  ctx->forever = (ctx->cnt ? 0 : 1);
  ctx->i = 0;
  ping_req_init(ctx);
  return 0;
}


LWIP_STATIC void
ping_resp_code_print(u8_t iecho_resp)
{
  const char *printstr = NULL;
  LWIP_STATIC const char *icmp_code_str[] = {
    "icmp reply", /* ICMP_ER */
    NULL,
    NULL,
    "destination host unreachable", /* ICMP_DUR */
    "source quench", /* ICMP_SQ */
    "redirect", /* ICMP_RD */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "time exceeded", /* ICMP_TE */
    "parameter problem" /* ICMP_PP */
  };

  if (iecho_resp < LWIP_ARRAYSIZE(icmp_code_str)) {
    printstr = icmp_code_str[iecho_resp];
  }

  if (printstr == NULL) {
    printstr = "unknow error";
  }
#ifdef CUSTOM_AT_COMMAND
  (void)uapi_at_printf("Ping: %s: "CRLF, printstr);
#else
  LWIP_PLATFORM_PRINT("Ping: %s: "CRLF, printstr);
#endif
}

LWIP_STATIC void
ping_resp_process(struct ping_run_ctx *ctx)
{
  u8_t af = AF_INET;
  const char *ipstr = NULL;
  char str_buf[PING_ADDR_STR_LEN];
  const void *src = &(((struct sockaddr_in *)&ctx->to)->sin_addr);

#if LWIP_IPV6
  if (ctx->is_v6) {
    af = AF_INET6;
    src = &(((struct sockaddr_in6 *)&ctx->to)->sin6_addr);
  }
#endif
  ipstr = lwip_inet_ntop(af, src, str_buf, PING_ADDR_STR_LEN);
  if (ipstr == NULL) {
    ipstr = "****";
  }

#ifdef CUSTOM_AT_COMMAND
#if LWIP_IPV6
  if (ctx->is_v6) {
    (void)uapi_at_printf("%u bytes from %s: icmp_seq=%u ", ctx->data_len, ipstr, ctx->i);
    if (ctx->rtt < 1) {
      (void)uapi_at_printf("time<1ms"CRLF);
    } else {
      (void)uapi_at_printf("time=%ims"CRLF, ctx->rtt);
    }
  } else
#endif
  {
    (void)uapi_at_printf("[%u]Reply from %s:", ctx->i, ipstr);
    if (ctx->rtt < 1) {
      (void)uapi_at_printf("time<1ms ");
    } else {
      (void)uapi_at_printf("time=%dms ", ctx->rtt);
    }
    (void)uapi_at_printf("TTL=%u"CRLF, ctx->iphdr_resp->_ttl);
  }
#else
  LWIP_PLATFORM_PRINT("[%u]Reply from %s: ", ctx->i, ipstr);
  if (ctx->rtt < 1) {
    LWIP_PLATFORM_PRINT("time<1 ms ");
  } else {
    LWIP_PLATFORM_PRINT("time=%i ms ", ctx->rtt);
  }
  if (ctx->is_v6 == 0) {
    LWIP_PLATFORM_PRINT("TTL=%u"CRLF, ctx->iphdr_resp->_ttl);
  } else {
    LWIP_PLATFORM_PRINT(CRLF);
  }
#endif

  /* delay 1s for every successful ping */
  ctx->intrvl = ctx->interval;
  if (((ctx->succ_cnt + ctx->failed_cnt + 1) < ctx->cnt) || ctx->forever) {
    do {
      if (ctx->intrvl < MS_PER_SECOND) {
        sys_msleep(ctx->intrvl);
        break;
      }
      ctx->intrvl -= MS_PER_SECOND;
      sys_msleep(MS_PER_SECOND);
      if (ping_kill == 1) {
        break;
      }
    } while (ctx->intrvl > 0);
  }
  ctx->succ_cnt++;
#ifdef CUSTOM_AT_COMMAND
  if (ctx->rtt >= 1) {
    ctx->rtt_sum += ctx->rtt;
  }
  if ((ctx->rtt < ctx->rtt_min) || (ctx->succ_cnt == 1)) {
    ctx->rtt_min = ctx->rtt;
  }
  if ((ctx->rtt > ctx->rtt_max) || (ctx->succ_cnt == 1)) {
    ctx->rtt_max = ctx->rtt;
  }
#endif
}

#if LWIP_LITEOS_TASK
td_u64 uapi_get_us(td_void)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0) {
        return (td_u64)(td_u32)tp.tv_sec * SEC_TO_US + (td_u32)tp.tv_nsec / US_TO_NSEC;
    } else {
        return (td_u64)(EXT_ERR_FAILURE);
    }
}
#endif

LWIP_STATIC void
ping_rcv_rtt_calc(struct ping_run_ctx *ctx)
{
#if LWIP_LITEOS_TASK
  ctx->timout_end_us = uapi_get_us();
  if (ctx->timout_end_us < ctx->start_us) {
    /* Incase of wraparoud of ticks */
    ctx->rtt = (s32_t)(ctx->timout_end_us / US_PER_MSECOND + ((LWIP_MAX_TICK_U64 - ctx->start_us) / US_PER_MSECOND));
  } else {
    ctx->rtt = (s32_t)(ctx->timout_end_us / US_PER_MSECOND - ctx->start_us / US_PER_MSECOND);
  }
#else
  ctx->timout_end_ms = sys_now();
  ctx->rtt = (s32_t)(ctx->timout_end_ms - ctx->start_ms);
  if (ctx->rtt < 0) {
    ctx->rtt = (s32_t)(ctx->timout_end_ms + (LWIP_MAX_TICK - ctx->start_ms));
  }
#endif
}

#if LWIP_IPV6
LWIP_STATIC const char *convert_icmpv6_err_to_string(u8_t err_type)
{
  switch (err_type) {
    case ICMP6_TYPE_DUR:
      return "Destination Unreachable";
    case ICMP6_TYPE_PTB:
      return "Packet too big";
    case ICMP6_TYPE_TE:
      return "Time Exceeded";
    case ICMP6_TYPE_PP:
      return "Parameter Problem";
    default:
      break;
  }
  return NULL;
}
#endif

LWIP_STATIC int
ping_rcv_check(struct ping_run_ctx *ctx, int len)
{
#if LWIP_IPV6
  int ip_hlen_min = ctx->is_v6 ? 0 : IP_HLEN;
#else
  int ip_hlen_min = IP_HLEN;
#endif

  if ((u32_t)len < sizeof(struct icmp_echo_hdr) + ip_hlen_min) {
#ifdef CUSTOM_AT_COMMAND
    /* Drop the packet if its too short [Doesnot contain even the header !!] */
    (void)uapi_at_printf("ping: received ICMP echo response too short for less icmp header\n");
#else
    LWIP_PLATFORM_PRINT("Ping: received ICMP echo response too short for less icmp header\n");
#endif
    return -1;
  }

  /* Accessing ip header and icmp header */
#if LWIP_IPV6
  ctx->ip_hlen = ctx->is_v6 ? 0 : (IPH_HL(ctx->iphdr_resp) << 2);
#else
  ctx->ip_hlen = (IPH_HL(ctx->iphdr_resp) << 2);
#endif
  if ((u32_t)len < sizeof(struct icmp_echo_hdr) + ctx->ip_hlen) {
#ifdef CUSTOM_AT_COMMAND
    /* Drop the packet if its too short [Doesnot contain send len !!] */
    (void)uapi_at_printf("ping : received ICMP echo response too short for not equal send len\n");
#else
    LWIP_PLATFORM_PRINT("Ping: received ICMP echo response too short for not equal send len\n");
#endif
    return -1;
  }

#if LWIP_IPV6
  if (ctx->is_v6) {
    ctx->iecho_resp = (struct icmp_echo_hdr *)((char *)ctx->iphdr_resp);
  } else
#endif
  {
    struct sockaddr_in *sin = (struct sockaddr_in *)&ctx->to;
    if (sin->sin_addr.s_addr != ctx->iphdr_resp->src.addr) {
      return -1;
    }
    ctx->iecho_resp = (struct icmp_echo_hdr *)((char *)ctx->iphdr_resp + ctx->ip_hlen);
  }

#if LWIP_IPV6
  if (ctx->is_v6) {
     if (ICMPH_TYPE(ctx->iecho_resp) != ICMP6_TYPE_EREP) {
        const char *err_str = convert_icmpv6_err_to_string(ICMPH_TYPE(ctx->iecho_resp));
        if (err_str != NULL) {
#ifdef CUSTOM_AT_COMMAND
          (void)uapi_at_printf("ping: %s"CRLF, err_str);
#else
          LWIP_PLATFORM_PRINT("ping: %s"CRLF, err_str);
#endif
        }
        return -1;
     }
  } else
#endif
  {
    if (ICMPH_TYPE(ctx->iecho_resp) == ICMP_ECHO) {
      /* skip loopback ICMP_ECHO */
      return -1;
    } else if (ICMPH_TYPE(ctx->iecho_resp) != ICMP_ER) {
      ping_resp_code_print(ICMPH_TYPE(ctx->iecho_resp));
      return -1;
    }
  }

  if (ctx->iecho_resp->id != ctx->iecho->id) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Ping : recv id unmatch"CRLF);
#else
    LWIP_PLATFORM_PRINT("Ping: recv id unmatch %u %u"CRLF, ctx->iecho_resp->id, ctx->iecho->id);
#endif
    return -1;
  }

  if (ntohs(ctx->iecho_resp->seqno) != ctx->i) {
#ifndef CUSTOM_AT_COMMAND
    LWIP_PLATFORM_PRINT("Ping: recv seqno unmatch"CRLF);
#endif
    return -1;
  }

  return 0;
}

LWIP_STATIC int
ping_rcv_poll(struct ping_run_ctx *ctx)
{
#if LWIP_SOCKET_POLL && !LWIP_EXT_POLL_SUPPORT
  /* poll for ICMP echo response msg */
  ctx->pfd.fd = ctx->sfd;
  ctx->pfd.events = POLLIN;
  ctx->pfd.revents = 0;
  return poll(&ctx->pfd, 1, ctx->timout_ms);
#else
  /* Wait in select for ICMP response msg */
  FD_ZERO(&ctx->read_set);
  FD_SET(ctx->sfd, &ctx->read_set);
  ctx->time_val.tv_sec = ctx->timout_ms / MS_PER_SECOND;
  ctx->time_val.tv_usec = (ctx->timout_ms % MS_PER_SECOND) * US_PER_MSECOND;
  return lwip_select(ctx->sfd + 1, &ctx->read_set, 0, 0, &ctx->time_val);
#endif
}

LWIP_STATIC int
ping_rcv(struct ping_run_ctx *ctx)
{
  int ret;
  /* capture the start ms to calculate RTT */
#if LWIP_LITEOS_TASK
  ctx->start_us = uapi_get_us();
#else
  ctx->start_ms = sys_now();
#endif
  ctx->timout_ms = (s32_t)ctx->timout;

  do {
    ret = ping_rcv_poll(ctx);
    if (ret < 0) {
#ifdef CUSTOM_AT_COMMAND
      (void)uapi_at_printf("ping : poll/select failure, errno = %d"CRLF, errno);
#else
      LWIP_PLATFORM_PRINT("ping : poll/select failure, errno = %d"CRLF, errno);
#endif
      return -1;
    } else if (ret == 0) {
      /* first type timeout event */
      break;
    }

    /* construct timeout event if poll lose efficacy when other host ping us */
    ret = recv(ctx->sfd, ctx->iphdr_resp, ctx->iecho_len + IP_HLEN_MAX, MSG_DONTWAIT);
    if (ret < 0) {
#ifdef CUSTOM_AT_COMMAND
      (void)uapi_at_printf(CRLF"Recv failed errno = %d"CRLF, errno);
#else
      LWIP_PLATFORM_PRINT(CRLF"Recv failed errno = %d"CRLF, errno);
#endif
      return -1;
    }
    ping_rcv_rtt_calc(ctx);
    ctx->timout_ms = ctx->timout - ctx->rtt;
    if (ping_rcv_check(ctx, ret) == 0) {
      ping_resp_process(ctx);
      ctx->i++;
      return 0;
    }
  } while (ctx->timout_ms > 0);

  /* all timeout events are true timeout */
  ctx->i++;
  ctx->failed_cnt++;
#ifdef CUSTOM_AT_COMMAND
  (void)uapi_at_printf(CRLF"Ping: destination unreachable"CRLF);
#else
  LWIP_PLATFORM_PRINT("Ping: destination unreachable"CRLF);
#endif
  return 0;
}

LWIP_STATIC void
ping_result_print(struct ping_run_ctx *ctx)
{
#ifdef CUSTOM_AT_COMMAND
  (void)uapi_at_printf("%u packets transmitted, %u received, ", ctx->succ_cnt + ctx->failed_cnt, ctx->succ_cnt);
#if LWIP_IPV6
  if (ctx->is_v6) {
    float loss = ((float)ctx->failed_cnt * PERCENT) / (ctx->succ_cnt + ctx->failed_cnt);
    (void)uapi_at_printf("%.2f%% loss", loss);
  } else
#endif
  {
    (void)uapi_at_printf("%u loss", ctx->failed_cnt);
  }

  if (ctx->succ_cnt > 0) {
    s32_t rtt_ave = ctx->rtt_sum / (s32_t)ctx->succ_cnt;
    (void)uapi_at_printf(", rtt min/avg/max = %d/%d/%d ms"CRLF, ctx->rtt_min, rtt_ave, ctx->rtt_max);
    (void)uapi_at_printf(CRLF"OK"CRLF);
  } else {
    (void)uapi_at_printf(CRLF"ERROR"CRLF);
  }
#else
  u8_t af = AF_INET;
  const char *ipstr = NULL;
  char str_buf[PING_ADDR_STR_LEN];
  const void *src = &(((struct sockaddr_in *)&ctx->to)->sin_addr);

#if LWIP_IPV6
  if (ctx->is_v6) {
    af = AF_INET6;
    src = &(((struct sockaddr_in6 *)&ctx->to)->sin6_addr);
  }
#endif
  ipstr = lwip_inet_ntop(af, src, str_buf, PING_ADDR_STR_LEN);
  if (ipstr == NULL) {
    ipstr = "****";
  }

  LWIP_PLATFORM_PRINT(CRLF"--- %s ping statistics ---"CRLF, ipstr);
  LWIP_PLATFORM_PRINT("%u packets transmitted, %u received, %u loss"CRLF, ctx->succ_cnt + ctx->failed_cnt,
         ctx->succ_cnt, ctx->failed_cnt);
  LWIP_PLATFORM_PRINT(CRLF"OK"CRLF);
#endif
}

LWIP_STATIC int ping_setsockopt(struct ping_run_ctx *ctx, struct ping_cfg *cfg)
{
  int ret;
  if (cfg->src_type == LWIP_PING_HOSTNAME_ARG) {
    ret = lwip_bind(ctx->sfd, (struct sockaddr *)&cfg->src, (socklen_t)ctx->socklen);
    if (ret == -1) {
#if CUSTOM_AT_COMMAND
      (void)uapi_at_printf("ping bind fail errno = %d"CRLF, errno);
#else
      LWIP_PLATFORM_PRINT("ping bind fail errno = %d"CRLF, errno);
#endif
      return -1;
    }
  } else if (cfg->src_type == LWIP_PING_INTERFACE_ARG) {
    struct ifreq req;
    (void)memset_s(&req, sizeof(req), 0, sizeof(req));
    ret = memcpy_s(req.ifr_ifrn.ifrn_name, sizeof(req.ifr_ifrn.ifrn_name) - 1, cfg->src_iface, strlen(cfg->src_iface));
    if (ret != EOK) {
      (void)lwip_close(ctx->sfd);
      return -1;
    }

    /* Binding socket to the provided netif */
    ret = lwip_setsockopt(ctx->sfd, SOL_SOCKET, SO_BINDTODEVICE, &req, sizeof(req));
    if (ret == -1) {
      LWIP_PLATFORM_PRINT("ping: unknown iface %s errno %d"CRLF, cfg->src_iface, errno);
      return -1;
    }
  }

#if LWIP_IPV6 && LWIP_SOCK_OPT_ICMP6_FILTER
  if (ctx->is_v6) {
    struct icmp6_filter icmp6_sock_filter;
    /* Setting socket filter since we are interested only in ECHO REPLY and ERROR messages */
    ICMP6_FILTER_SETBLOCKALL(&icmp6_sock_filter);
    ICMP6_FILTER_SETPASS(ICMP6_TYPE_EREP, &icmp6_sock_filter);
    ICMP6_FILTER_SETPASS(ICMP6_TYPE_DUR, &icmp6_sock_filter);
    ICMP6_FILTER_SETPASS(ICMP6_TYPE_PTB, &icmp6_sock_filter);
    ICMP6_FILTER_SETPASS(ICMP6_TYPE_TE, &icmp6_sock_filter);

    ret = lwip_setsockopt(ctx->sfd, IPPROTO_ICMPV6, ICMP6_FILTER, &icmp6_sock_filter, sizeof(struct icmp6_filter));
    if (ret == -1) {
      LWIP_PLATFORM_PRINT("ping : setsockopt: errno %d"CRLF, errno);
      return -1;
    }
  }
#endif /* LWIP_SOCK_OPT_ICMP6_FILTER */
  return 0;
}

/* as this is not an internal function removing static keyword */
LWIP_STATIC void
os_ping_func(void* para)
{
  LWIP_ERROR("os_ping_func para is null\n", para != NULL, return;);
  struct ping_cfg *cfg = (struct ping_cfg *)para;

  struct ping_run_ctx ctx;
  int ret = -1;

  if (ping_ctx_init(&ctx, cfg) != 0) {
    mem_free(cfg);
    goto FAILURE;
  }

  if (ctx.is_v6 == 0) {
    ctx.sfd = lwip_socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
  }
#if LWIP_IPV6
  else {
    ctx.sfd = lwip_socket(PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  }
#endif
  if (ctx.sfd < 0) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Ping socket create fail, errno = %d"CRLF, errno);
#else
    LWIP_PLATFORM_PRINT("Ping socket create fail, errno = %d"CRLF, errno);
#endif
    mem_free(cfg);
    goto FAILURE;
  }

  if (ping_setsockopt(&ctx, cfg) == -1) {
    mem_free(cfg);
    goto FAILURE;
  }

  mem_free(cfg);
#ifdef CUSTOM_AT_COMMAND
  if (ctx.is_v6 == 0) {
    (void)uapi_at_printf("+PING:"CRLF);
  } else {
    (void)uapi_at_printf("+PING6:"CRLF);
  }
#endif
  ping_task_running = 1;
  while ((ping_kill == 0) && ((ctx.forever != 0) || ((ctx.succ_cnt + ctx.failed_cnt) < ctx.cnt))) {
    ctx.iecho->seqno = htons((u16_t)ctx.i);
    ctx.iecho->chksum = 0;
    ctx.iecho->chksum = inet_chksum((void *)ctx.iecho, ctx.iecho_len);
    ret = sendto(ctx.sfd, ctx.iecho, ctx.iecho_len, 0, (struct sockaddr *)&ctx.to, (socklen_t)ctx.socklen);
    if (ret < 0) {
#ifdef CUSTOM_AT_COMMAND
      (void)uapi_at_printf("Ping: sending ICMP echo request fail errno %d"CRLF, errno);
#else
      LWIP_PLATFORM_PRINT("Ping: sending ICMP echo request fail errno %d"CRLF, errno);
#endif
      goto FAILURE;
    }

    if (ping_rcv(&ctx) != 0) {
      break;
    }
  }

  ping_result_print(&ctx);
#ifdef CONFIG_SIGMA_SUPPORT
  if(upload_ping_result) {
    upload_ping_result(ctx.succ_cnt, ctx.failed_cnt);
  }
#endif
  ret = (ctx.succ_cnt > 0) ? 0 : -1;
FAILURE:
  ping_kill = 0;
  ping_taskid = LWIP_DEFAULT_PING_TASK_ID;
  ping_task_running = 0;
  if (ctx.sfd >= 0) {
    (void)lwip_close(ctx.sfd);
  }
  if (ctx.iphdr_resp != NULL) {
    mem_free(ctx.iphdr_resp);
  }
  if (ctx.iecho != NULL) {
    mem_free(ctx.iecho);
  }
}

#if LWIP_LITEOS_TASK
static void
ping_cmd(unsigned int p0, unsigned int p1, unsigned int p2, unsigned int p3)
{
  union los_ptr_args ptr_args;

  ptr_args.args[0] = p0;
  ptr_args.args[1] = p1;
  os_ping_func((struct ping_cfg *)ptr_args.ptr);

  ping_taskid = LWIP_DEFAULT_PING_TASK_ID;
  (void)p2;
  (void)p3;
}
#endif

struct ping_cfg_handle {
  const char *key;
  u32_t *data;
  int min;
  int max;
  int arg_num;
  int (*handle)(struct ping_cfg_handle *src_handle, const char *arg);
};

LWIP_STATIC int
ping_cfg_parse_daddr(const char *daddr, struct sockaddr_storage *dst_ipaddr, u32_t is_v6)
{
  dst_ipaddr->ss_family = AF_INET;
#if LWIP_IPV6
  if (is_v6) {
    dst_ipaddr->ss_family = AF_INET6;
  }
#endif
  /* initialize dst IP address */
#if LWIP_DNS
  struct addrinfo hints_structure;
  struct addrinfo *res = NULL;

  /* Resolve the given hostname */
  hints_structure.ai_family = dst_ipaddr->ss_family;
  hints_structure.ai_flags = 0;
  if (lwip_getaddrinfo(daddr, NULL, &hints_structure, &res) != ERR_OK) {
    goto fail;
  }

#if LWIP_IPV6
  if (is_v6) {
    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)dst_ipaddr;
    sin6->sin6_addr = ((struct sockaddr_in6 *)(res->ai_addr))->sin6_addr;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = 0;
    sin6->sin6_scope_id = 0;
    sin6->sin6_flowinfo = 0;
  } else
#endif
  {
    struct sockaddr_in *sin = (struct sockaddr_in *)dst_ipaddr;
    sin->sin_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
    sin->sin_family = AF_INET;
    sin->sin_port = 0;
  }
  lwip_freeaddrinfo(res);
#else /* LWIP_DNS */
  void *buf = NULL;
#if LWIP_IPV6
  if (is_v6) {
    buf = &(((struct sockaddr_in6 *)dst_ipaddr)->sin6_addr);
  } else
#endif
  {
    buf = &(((struct sockaddr_in *)dst_ipaddr)->sin_addr);
  }

  if (lwip_inet_pton(dst_ipaddr->ss_family, daddr, buf) != 1) {
    goto fail;
  }
#endif /* LWIP_DNS */

#if LWIP_IPV6
  if (is_v6) {
    ip6_addr_t ip6;
    inet6_addr_to_ip6addr(&ip6, &((struct sockaddr_in6 *)dst_ipaddr)->sin6_addr);
    if (ip6_addr_isany(&ip6) || ip6_addr_isnone(&ip6)) {
      goto fail;
    }
  } else
#endif
  {
    ip4_addr_t ip4;
    ip4.addr = ((struct sockaddr_in *)dst_ipaddr)->sin_addr.s_addr;
    if ((ip4.addr == IPADDR_NONE) || (ip4.addr == IPADDR_ANY)) {
      goto fail;
    }
  }
  return 0;

fail:
#ifdef CUSTOM_AT_COMMAND
  (void)uapi_at_printf("Host:%s can't be resolved to IP"CRLF, daddr);
#else
  LWIP_PLATFORM_PRINT("Host:%s can't be resolved to IP"CRLF, daddr);
#endif
  return -1;
}

LWIP_STATIC int ping_cfg_handle_src_addr(struct ping_cfg_handle *src_handle, const char *arg)
{
  struct ping_cfg *cfg = (struct ping_cfg *)src_handle->data;
  u16_t family = AF_INET;
  void *buf = &(((struct sockaddr_in *)&cfg->src)->sin_addr);

#if LWIP_IPV6
  if (cfg->is_v6) {
    family = AF_INET6;
    buf = &(((struct sockaddr_in6 *)&cfg->src)->sin6_addr);
  }
#endif
  if (lwip_inet_pton(family, arg, buf) != 1) {
    if (strcpy_s(cfg->src_iface, NETIF_NAMESIZE, arg) != EOK) {
      return -1;
    }

    cfg->src_type = LWIP_PING_INTERFACE_ARG;
    return 0;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)&cfg->src;
  sin->sin_family = family;
  cfg->src_type = LWIP_PING_HOSTNAME_ARG;
  return 0;
}

LWIP_STATIC int
ping_cfg_parse(int argc, const char **argv, struct ping_cfg *cfg, u32_t *ping_kill_flag)
{
  u32_t ping_forever = 0;
  u32_t i = 0;
  u32_t j = 0;
  s64_t ret;
  struct ping_cfg_handle handle[] = {
    {
      .key = "-k",
      .data = ping_kill_flag,
      .min = 0,
      .max = INT_MAX,
      .arg_num = 0,
      .handle = NULL,
    },
    {
      .key = "-n",
      .data = &cfg->count,
      .min = 1,
      .max = INT_MAX,
      .arg_num = 1,
      .handle = NULL,
    },
    {
      .key = "-c",
      .data = &cfg->count,
      .min = 1,
      .max = INT_MAX,
      .arg_num = 1,
      .handle = NULL,
    },
    {
      .key = "-t",
      .data = &ping_forever,
      .min = 0,
      .max = INT_MAX,
      .arg_num = 0,
      .handle = NULL,
    },
    {
      .key = "-w",
      .data = &cfg->interval,
      .min = 1,
      .max = INT_MAX,
      .arg_num = 1,
      .handle = NULL,
    },
    {
      .key = "-W",
      .data = &cfg->timeout,
      .min = LWIP_SHELL_CMD_PING_TIMEOUT_MIN,
      .max = LWIP_SHELL_CMD_PING_TIMEOUT_MAX,
      .arg_num = 1,
      .handle = NULL,
    },
    {
      .key = "-l",
      .data = &cfg->data_len,
      .min = 0,
#ifndef CUSTOM_AT_COMMAND
      .max = (int)(LWIP_MAX_UDP_RAW_SEND_SIZE - sizeof(struct icmp_echo_hdr)) - PBUF_ZERO_COPY_RESERVE,
#else
      .max = LWIP_PING_MAX_PKT_LEN,
#endif
      .arg_num = 1,
      .handle = NULL,
    },
    {
      .key = "-6",
      .data = &cfg->is_v6,
      .min = 0,
      .max = INT_MAX,
      .arg_num = 0,
      .handle = NULL,
    },
    {
      .key = "-I",
      .data = (u32_t *)cfg,
      .min = 0,
      .max = INT_MAX,
      .arg_num = 1,
      .handle = ping_cfg_handle_src_addr,
    },
  };
  cfg->count = LWIP_SHELL_CMD_PING_RETRY_TIMES;
  cfg->is_v6 = 0;
  /* could add more param support */
  while (argc > 0) {
    u8_t match = 0;
    for (j = 0; j < LWIP_ARRAYSIZE(handle); j++) {
      if (strcmp(argv[i], handle[j].key) == 0) {
        if ((handle[j].arg_num + 1) > argc) {
          goto param_error;
        }

        if (handle[j].arg_num == 0) {
          *(handle[j].data) = 1;
          if (ping_kill_flag == handle[j].data) {
            return 0;
          }
        } else if (handle[j].handle != NULL) {
          ret = (*handle[j].handle)(&handle[j], argv[i + 1]);
          if (ret != 0) {
#ifdef CUSTOM_AT_COMMAND
            (void)uapi_at_printf("Ping %s arg error"CRLF, handle[j].key);
#else
            LWIP_PLATFORM_PRINT("Ping %s arg error"CRLF, handle[j].key);
#endif
            return -1;
          }
        } else {
          char *invalid_ptr;
          ret = strtoll(argv[i + 1], &invalid_ptr, 10);
          if(invalid_ptr && strlen(invalid_ptr) != 0) {
            LWIP_PLATFORM_PRINT("Invalid parameter: |%s|\n", argv[i + 1]);
            return -1;
          }

          if ((ret < (s64_t)handle[j].min) || (ret > (s64_t)handle[j].max)) {
#ifdef CUSTOM_AT_COMMAND
            (void)uapi_at_printf("Ping %s arg error, range:[%d, %d] "CRLF,
                               handle[j].key, handle[j].min, handle[j].max);
#else
            LWIP_PLATFORM_PRINT("Ping %s arg error, range:[%d, %d] "CRLF,
                   handle[j].key, handle[j].min, handle[j].max);
#endif
            return -1;
          }
          *(handle[j].data) = (u32_t)ret;
        }
        i += handle[j].arg_num + 1;
        argc -= handle[j].arg_num + 1;
        match = 1;
        break;
      }
    }

    if (match == 0) {
      break;
    }
  }

  if (argc != 1) {
param_error:
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Invalid param"CRLF);
#else
    LWIP_PLATFORM_PRINT("Invalid param"CRLF);
#endif
    return -1;
  }

  if (ping_cfg_parse_daddr(argv[i], &cfg->dst, cfg->is_v6) == -1) {
    return -1;
  }

  if (ping_forever == 1) {
    cfg->count = 0;
  }
  return 0;
}

#if LWIP_LITEOS_TASK
LWIP_STATIC u32_t ping_task_create(struct ping_cfg *cfg, u16_t prio)
{
  UINT32 los_ret;
  TSK_INIT_PARAM_S st_ping_task;
  union los_ptr_args ptr_args = {0};

  /* start one task if ping forever or ping count greater than 60 */
  if (ping_taskid != LWIP_DEFAULT_PING_TASK_ID) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Ping task is running"CRLF);
#else
    LWIP_PLATFORM_PRINT("Ping task is running"CRLF);
#endif
    return OS_NOK;
  }
  ptr_args.ptr = cfg;

  st_ping_task.pfnTaskEntry = (TSK_ENTRY_FUNC)ping_cmd;
  st_ping_task.uwStackSize  = 4096;
  st_ping_task.pcName = "ping_task";
  st_ping_task.usTaskPrio = prio; /* equal lwip */
  st_ping_task.uwResved = LOS_TASK_STATUS_DETACHED;
#ifdef LOSCFG_OBSOLETE_API
  st_ping_task.auwArgs[0] = ptr_args.args[0];
  st_ping_task.auwArgs[1] = ptr_args.args[1];
  st_ping_task.auwArgs[2] = 0;
  st_ping_task.auwArgs[3] = 0;
#else
  st_ping_task.pArgs=(void*)(ptr_args.args[0]);
#endif
  los_ret = LOS_TaskCreate((UINT32 *)(&ping_taskid), &st_ping_task);
  if (los_ret != OS_OK) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("ping_task create fail 0x%08x."CRLF, los_ret);
#else
    LWIP_PLATFORM_PRINT("ping_task create fail 0x%08x."CRLF, los_ret);
#endif
    return OS_NOK;
  }
  return OS_OK;
}
#endif

LWIP_STATIC u32_t ping_exec(struct ping_cfg *cfg)
{
#ifdef CUSTOM_AT_COMMAND
#if LWIP_LITEOS_TASK
  u32_t ret = ping_task_create(cfg, LWIP_PING_TASK_PRIO);
  if (ret != OS_OK) {
    mem_free(cfg);
  }
  return ret;
#endif
#else
#if LWIP_LITEOS_TASK
  /* start one task if ping forever or ping count greater than 60 */
  if ((cfg->count == 0) || (cfg->count > LWIP_SHELL_CMD_PING_RETRY_TIMES)) {
    u32_t ret = ping_task_create(cfg, LWIP_PING_TASK_PRIO);
    if ((ret == OS_NOK) || (ping_taskid == LWIP_DEFAULT_PING_TASK_ID)) {
      cfg->count = LWIP_SHELL_CMD_PING_RETRY_TIMES;
    } else {
      return OS_OK;
    }
  }
#endif
  /* Use threads to perform ping operations. */
  ping_taskid = sys_thread_new("ping_thread", os_ping_func, (void*)cfg, TCPIP_THREAD_STACKSIZE, 8);
#if !LWIP_FREERTOS_COMPAT
  /* Bugfix: The processing of the thread return is slower than that of the os_ping_func function. */
  ping_taskid = ping_task_running == 0 ? LWIP_DEFAULT_PING_TASK_ID : ping_taskid;
#endif
  return OS_OK;
#endif
}

u32_t
os_shell_ping(int argc, const char **argv)
{
  struct ping_cfg *cfg = NULL;
  u32_t ping_kill_flag = 0;

  if (tcpip_init_finish == 0) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("shell_ping tcpip_init have not been called"CRLF);
#else
    LWIP_PLATFORM_PRINT("shell_ping tcpip_init have not been called"CRLF);
#endif
    return OS_NOK;
  }

  cfg = mem_malloc(sizeof(struct ping_cfg));
  if (cfg == NULL) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("shell_ping malloc fail"CRLF);
#else
    LWIP_PLATFORM_PRINT("shell_ping malloc fail"CRLF);
#endif
    return OS_NOK;
  }
  cfg->count = 0;
  cfg->interval = 1000; /* default ping interval */
  cfg->timeout = LWIP_SHELL_CMD_PING_TIMEOUT;
  cfg->data_len = 48; /* default data length */
  cfg->src_type = LWIP_PING_DEFAULT_SOCKET;
  cfg->is_v6 = 0;

  if ((argc < 1) || (argv == NULL)) {
#ifdef CUSTOM_AT_COMMAND
    (void)uapi_at_printf("Ping: require dest ipaddr at least "CRLF);
#else
    LWIP_PLATFORM_PRINT("Ping: require dest ipaddr at least "CRLF);
#endif
    goto ping_error;
  }
  if (ping_cfg_parse(argc, argv, cfg, &ping_kill_flag) == -1) {
    goto ping_error;
  }

  if (ping_kill_flag == 1) {
    u32_t is_v6 = cfg->is_v6;
    mem_free(cfg);
    if (ping_taskid != LWIP_DEFAULT_PING_TASK_ID) {
      ping_kill = 1; /* stop the current ping task */
#ifdef CUSTOM_AT_COMMAND
      (void)uapi_at_printf("+PING%s:"CRLF"OK"CRLF, is_v6 ? "6" : "");
#else
      LWIP_UNUSED_ARG(is_v6);
#endif
      return (int)OS_OK;
    } else {
      LWIP_PLATFORM_PRINT("No ping task running"CRLF);
      return (int)OS_NOK;
    }
  }

  if(ping_task_running == 1) {
    LWIP_PLATFORM_PRINT("Now ping task is running"CRLF);
    mem_free(cfg);
    return OS_NOK;
  }

  return ping_exec(cfg);
ping_error:
  lwip_ping_usage(cfg->is_v6);
  mem_free(cfg);
  return OS_NOK;
}

#if LWIP_IPV6
u32_t os_shell_ping6(int argc, const char **argv)
{
    int i;
    u32_t ret;
    const char **argv6 = mem_malloc(sizeof(char *) * (argc + 1));
    if (argv6 == NULL) {
#ifdef CUSTOM_AT_COMMAND
      (void)uapi_at_printf("ping6 malloc fail"CRLF);
#else
      LWIP_PLATFORM_PRINT("ping6 malloc fail"CRLF);
#endif
      return OS_NOK;
    }

    argv6[0] = "-6";
    for (i = 0; i < argc; i++) {
      argv6[i + 1] = argv[i];
    }
    argc++;
    ret = os_shell_ping(argc, argv6);
    mem_free(argv6);
    return ret;
}
#endif /* LWIP_IPV6 */

#endif /* LWIP_ENABLE_BASIC_SHELL_CMD */
