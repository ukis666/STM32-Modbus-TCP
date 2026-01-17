#include "app_modbus.h"
#include "app_config.h"
#include "app_regs.h"
#include "app_log.h"
#include "app_p10.h"
#include "app_supervisor.h"

#include "cmsis_os.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#pragma pack(push,1)
typedef struct {
  uint16_t tid;
  uint16_t pid;
  uint16_t len;
  uint8_t  uid;
} mbap_t;
#pragma pack(pop)

static uint16_t be16(const uint8_t* p)
{
  return (uint16_t)((p[0] << 8) | p[1]);
}

static void put_be16(uint8_t* p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
}

static int handle_pdu(const uint8_t* req_pdu, uint16_t req_len,
                      uint8_t* resp_pdu, uint16_t resp_cap)
{
  if (req_len < 1) return -1;
  uint8_t fc = req_pdu[0];

  /* FC03 - Read Holding Registers */
  if (fc == 0x03) {
    if (req_len < 5) return -1;

    uint16_t addr = be16(&req_pdu[1]);
    uint16_t qty  = be16(&req_pdu[3]);

    if (qty == 0 || qty > 125) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x03; // illegal data value
      return 2;
    }

    uint16_t tmp[125];
    if (APP_RegsReadHRBlock(addr, tmp, qty) != qty) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x02; // illegal data address
      return 2;
    }

    uint16_t bc = (uint16_t)(qty * 2);
    if ((uint32_t)bc + 2U > resp_cap) return -1;

    resp_pdu[0] = fc;
    resp_pdu[1] = (uint8_t)bc;
    for (uint16_t i = 0; i < qty; ++i) {
      put_be16(&resp_pdu[2 + i*2], tmp[i]);
    }
    return 2 + bc;
  }

  /* FC06 - Write Single Register */
  if (fc == 0x06) {
    if (req_len < 5) return -1;

    uint16_t addr = be16(&req_pdu[1]);
    uint16_t val  = be16(&req_pdu[3]);

    if (APP_RegsWriteHR(addr, val) != 1) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x02;
      return 2;
    }

    /* time update hook */
    uint16_t m, s;
    APP_RegsGetTime(&m, &s);
    APP_LogNotifyTime(m, s);
    APP_P10_SetTime(m, s);

    /* echo request */
    if (resp_cap < 5) return -1;
    memcpy(resp_pdu, req_pdu, 5);
    return 5;
  }

  /* FC16 (0x10) - Write Multiple Registers */
  if (fc == 0x10) {
    if (req_len < 6) return -1;

    uint16_t addr = be16(&req_pdu[1]);
    uint16_t qty  = be16(&req_pdu[3]);
    uint8_t  bc   = req_pdu[5];

    if (qty == 0 || qty > 123 || bc != (uint8_t)(qty * 2) || req_len < (uint16_t)(6 + bc)) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x03;
      return 2;
    }

    uint16_t tmp[123];
    for (uint16_t i = 0; i < qty; ++i) {
      tmp[i] = be16(&req_pdu[6 + i*2]);
    }

    if (APP_RegsWriteHRBlock(addr, tmp, qty) != qty) {
      resp_pdu[0] = (uint8_t)(fc | 0x80);
      resp_pdu[1] = 0x02;
      return 2;
    }

    uint16_t m, s;
    APP_RegsGetTime(&m, &s);
    APP_LogNotifyTime(m, s);
    APP_P10_SetTime(m, s);

    if (resp_cap < 5) return -1;
    resp_pdu[0] = fc;
    put_be16(&resp_pdu[1], addr);
    put_be16(&resp_pdu[3], qty);
    return 5;
  }

  /* Unsupported function */
  resp_pdu[0] = (uint8_t)(fc | 0x80);
  resp_pdu[1] = 0x01; // illegal function
  return 2;
}

static void serve_conn(struct netconn *newconn)
{
  struct netbuf *inbuf = NULL;
  void *data = NULL;
  u16_t len = 0;

  for (;;) {
    err_t err = netconn_recv(newconn, &inbuf);

    /* Non-blocking sokette veri yoksa ERR_WOULDBLOCK gelir */
    if (err == ERR_WOULDBLOCK || err == ERR_TIMEOUT) {
      APP_SupervisorKick(APP_KICK_MODBUS);
      osDelay(10);
      continue;
    }

    if (err != ERR_OK) {
      break;
    }

    do {
      netbuf_data(inbuf, &data, &len);

      if (len >= 8) { // MBAP(7) + en az 1 byte FC
        const uint8_t* p = (const uint8_t*)data;

        mbap_t req;
        req.tid = be16(&p[0]);
        req.pid = be16(&p[2]);
        req.len = be16(&p[4]);
        req.uid = p[6];

        /* LEN = UID(1) + PDU(n). Tam ADU uzunlugu = 6 (TID..LEN) + LEN */
        uint16_t adu_len = (uint16_t)(6 + req.len);
        if (len < (u16_t)adu_len) {
          /* TCP parcali gelebilir; bu basit iskelette biriktirme yok -> atla */
          continue;
        }

        const uint8_t* req_pdu = &p[7];
        uint16_t req_pdu_len = (uint16_t)(adu_len - 7);

        uint8_t resp[260];
        put_be16(&resp[0], req.tid);
        put_be16(&resp[2], 0);
        resp[6] = req.uid;

        int pdu_len = handle_pdu(req_pdu, req_pdu_len, &resp[7],
                                 (uint16_t)(sizeof(resp) - 7));
        if (pdu_len > 0) {
          put_be16(&resp[4], (uint16_t)(pdu_len + 1));
          (void)netconn_write(newconn, resp, (size_t)(7 + pdu_len), NETCONN_COPY);
        }
      }

    } while (netbuf_next(inbuf) >= 0);

    netbuf_delete(inbuf);
    inbuf = NULL;

    APP_SupervisorKick(APP_KICK_MODBUS);
  }
}

void APP_ModbusTask(void *argument)
{
  (void)argument;

  struct netconn *conn = netconn_new(NETCONN_TCP);
  if (conn == NULL) {
    for (;;) { osDelay(1000); }
  }

  if (netconn_bind(conn, IP_ADDR_ANY, APP_MODBUS_TCP_PORT) != ERR_OK) {
    for (;;) { osDelay(1000); }
  }

  if (netconn_listen(conn) != ERR_OK) {
    for (;;) { osDelay(1000); }
  }

  /* Client yokken accept bloklamasin -> watchdog beslemeye devam */
  netconn_set_nonblocking(conn, 1);

  for (;;) {
    struct netconn *newconn = NULL;
    err_t err = netconn_accept(conn, &newconn);

    if (err == ERR_OK && newconn) {
      /* Client soketi de non-blocking: recv() veri yoksa ERR_WOULDBLOCK doner */
      netconn_set_nonblocking(newconn, 1);

      serve_conn(newconn);

      netconn_close(newconn);
      netconn_delete(newconn);
    } else {
      /* ERR_WOULDBLOCK dahil: veri yoksa burada kick atmaya devam */
      APP_SupervisorKick(APP_KICK_MODBUS);
      osDelay(50);
    }
  }
}
