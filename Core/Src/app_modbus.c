#include "app_modbus.h"

#include "app_regs.h"
#include "app_log.h"
#include "app_p10.h"

#include "cmsis_os.h"

#include "lwip/api.h"
#include "lwip/err.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------ helpers ------------------ */

static uint16_t be16_rd(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static void be16_wr(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
}

/* ------------------ Modbus PDU handler ------------------ */

static int handle_pdu(const uint8_t *req_pdu, uint16_t req_len,
                      uint8_t *resp_pdu, uint16_t resp_cap)
{
  if (req_len < 1) return -1;
  const uint8_t fc = req_pdu[0];

  /* 0x03 Read Holding Registers */
  if (fc == 0x03) {
    if (req_len < 5) return -1;
    const uint16_t addr = be16_rd(&req_pdu[1]);
    const uint16_t qty  = be16_rd(&req_pdu[3]);

    if (qty == 0 || qty > 125) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x03; /* ILLEGAL DATA VALUE */
      return 2;
    }

    uint16_t tmp[125];
    const uint16_t got = APP_RegsReadHRBlock(addr, tmp, qty);
    if (got != qty) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x02; /* ILLEGAL DATA ADDRESS */
      return 2;
    }

    const uint16_t bc = (uint16_t)(qty * 2);
    if ((uint32_t)bc + 2U > resp_cap) return -1;

    resp_pdu[0] = fc;
    resp_pdu[1] = (uint8_t)bc;
    for (uint16_t i = 0; i < qty; ++i) {
      be16_wr(&resp_pdu[2 + i * 2], tmp[i]);
    }
    return (int)(2 + bc);
  }

  /* 0x06 Write Single Holding Register */
  if (fc == 0x06) {
    if (req_len < 5) return -1;
    const uint16_t addr = be16_rd(&req_pdu[1]);
    const uint16_t val  = be16_rd(&req_pdu[3]);

    if (APP_RegsWriteHR(addr, val) != 1) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x02;
      return 2;
    }

    /* hook: if MMM/SS changed -> P10 + log */
    uint16_t m, s;
    if (APP_RegsConsumeChangedTime(&m, &s)) {
      APP_P10_SetTime(m, s);
      APP_LogNotifyTime(m, s);
    }

    /* echo request */
    if (resp_cap < 5) return -1;
    memcpy(resp_pdu, req_pdu, 5);
    return 5;
  }

  /* 0x10 Write Multiple Holding Registers */
  if (fc == 0x10) {
    if (req_len < 6) return -1;
    const uint16_t addr = be16_rd(&req_pdu[1]);
    const uint16_t qty  = be16_rd(&req_pdu[3]);
    const uint8_t  bc   = req_pdu[5];

    if (qty == 0 || qty > 123 || bc != (uint8_t)(qty * 2) || req_len < (uint16_t)(6 + bc)) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x03;
      return 2;
    }

    uint16_t tmp[123];
    for (uint16_t i = 0; i < qty; ++i) {
      tmp[i] = be16_rd(&req_pdu[6 + i * 2]);
    }

    if (APP_RegsWriteHRBlock(addr, tmp, qty) != qty) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x02;
      return 2;
    }

    /* hook: if MMM/SS changed -> P10 + log */
    uint16_t m, s;
    if (APP_RegsConsumeChangedTime(&m, &s)) {
      APP_P10_SetTime(m, s);
      APP_LogNotifyTime(m, s);
    }

    /* normal response: fc + addr + qty */
    if (resp_cap < 5) return -1;
    resp_pdu[0] = fc;
    be16_wr(&resp_pdu[1], addr);
    be16_wr(&resp_pdu[3], qty);
    return 5;
  }

  /* unsupported */
  resp_pdu[0] = (uint8_t)(fc | 0x80);
  resp_pdu[1] = 0x01; /* ILLEGAL FUNCTION */
  return 2;
}

/* ------------------ TCP stream reassembly ------------------ */

static void serve_conn(struct netconn *c)
{
  /* TCP is a stream: Modbus ADU may arrive split or coalesced. */
  uint8_t rx[512];
  size_t  used = 0;

  for (;;) {
    struct netbuf *inbuf = NULL;
    err_t err = netconn_recv(c, &inbuf);

    if (err == ERR_WOULDBLOCK) {
      osDelay(5);
      continue;
    }
    if (err != ERR_OK) {
      break; /* closed/reset/etc */
    }

    do {
      void *data = NULL;
      u16_t len = 0;
      netbuf_data(inbuf, &data, &len);

      if (len > 0 && data != NULL) {
        /* append to rx buffer */
        if (used + (size_t)len > sizeof(rx)) {
          /* overflow -> drop buffer (safe choice) */
          used = 0;
        } else {
          memcpy(&rx[used], data, (size_t)len);
          used += (size_t)len;
        }

        /* parse as many complete ADU as possible */
        for (;;) {
          if (used < 7) break; /* need at least MBAP(7) */

          const uint16_t tid = be16_rd(&rx[0]);
          const uint16_t pid = be16_rd(&rx[2]);
          const uint16_t mlen = be16_rd(&rx[4]); /* UID + PDU */

          /* sanity */
          if (pid != 0 || mlen < 2 || mlen > 253) {
            used = 0; /* resync hard */
            break;
          }

          const size_t adu_len = (size_t)6 + (size_t)mlen; /* (TID+PID+LEN)=6 + LEN */
          if (used < adu_len) break; /* wait more */

          const uint8_t uid = rx[6];
          const uint8_t *pdu = &rx[7];
          const uint16_t pdu_len = (uint16_t)(adu_len - 7);

          uint8_t tx[260];
          be16_wr(&tx[0], tid);
          be16_wr(&tx[2], 0);
          tx[6] = uid;

          int resp_pdu_len = handle_pdu(pdu, pdu_len, &tx[7], (uint16_t)(sizeof(tx) - 7));
          if (resp_pdu_len > 0) {
            be16_wr(&tx[4], (uint16_t)(resp_pdu_len + 1)); /* UID + PDU */
            (void)netconn_write(c, tx, (size_t)(7 + resp_pdu_len), NETCONN_COPY);
          }

          /* consume this ADU */
          const size_t rem = used - adu_len;
          if (rem > 0) memmove(rx, rx + adu_len, rem);
          used = rem;
        }
      }

    } while (netbuf_next(inbuf) >= 0);

    netbuf_delete(inbuf);
  }
}

/* ------------------ task ------------------ */

void APP_ModbusTask(void *argument)
{
  (void)argument;

  struct netconn *listener = netconn_new(NETCONN_TCP);
  if (listener == NULL) {
    for (;;) { osDelay(1000); }
  }

  netconn_bind(listener, IP_ADDR_ANY, APP_MODBUS_TCP_PORT);
  netconn_listen(listener);

  /* nonblocking accept loop */
  netconn_set_nonblocking(listener, 1);

  for (;;) {
    struct netconn *c = NULL;
    err_t err = netconn_accept(listener, &c);

    if (err == ERR_OK && c != NULL) {
      /* nonblocking recv -> no LWIP_SO_RCVTIMEO dependency */
      netconn_set_nonblocking(c, 1);

      serve_conn(c);

      netconn_close(c);
      netconn_delete(c);
    } else {
      /* ERR_WOULDBLOCK */
      osDelay(20);
    }
  }
}
