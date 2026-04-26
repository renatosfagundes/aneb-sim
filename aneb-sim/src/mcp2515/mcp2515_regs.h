/*
 * mcp2515_regs.h — register addresses and bit definitions.
 *
 * All values per Microchip MCP2515 datasheet (DS21801G). Register file
 * occupies addresses 0x00..0x7F; registers above that are mirrored or
 * undefined and are treated as 0x00 in our model.
 */
#ifndef ANEB_MCP2515_REGS_H
#define ANEB_MCP2515_REGS_H

#define MCP_REGS_SIZE   0x80    /* 128 byte register file */

/* ----- Acceptance filters (read-only outside Configuration mode) -------- */
#define MCP_RXF0SIDH    0x00
#define MCP_RXF0SIDL    0x01
#define MCP_RXF0EID8    0x02
#define MCP_RXF0EID0    0x03
#define MCP_RXF1SIDH    0x04
#define MCP_RXF1SIDL    0x05
#define MCP_RXF1EID8    0x06
#define MCP_RXF1EID0    0x07
#define MCP_RXF2SIDH    0x08
#define MCP_RXF2SIDL    0x09
#define MCP_RXF2EID8    0x0A
#define MCP_RXF2EID0    0x0B

#define MCP_BFPCTRL     0x0C    /* RXnBF pin control */
#define MCP_TXRTSCTRL   0x0D
#define MCP_CANSTAT     0x0E
#define MCP_CANCTRL     0x0F

#define MCP_RXF3SIDH    0x10
#define MCP_RXF3SIDL    0x11
#define MCP_RXF3EID8    0x12
#define MCP_RXF3EID0    0x13
#define MCP_RXF4SIDH    0x14
#define MCP_RXF4SIDL    0x15
#define MCP_RXF4EID8    0x16
#define MCP_RXF4EID0    0x17
#define MCP_RXF5SIDH    0x18
#define MCP_RXF5SIDL    0x19
#define MCP_RXF5EID8    0x1A
#define MCP_RXF5EID0    0x1B

#define MCP_TEC         0x1C
#define MCP_REC         0x1D

#define MCP_RXM0SIDH    0x20
#define MCP_RXM0SIDL    0x21
#define MCP_RXM0EID8    0x22
#define MCP_RXM0EID0    0x23
#define MCP_RXM1SIDH    0x24
#define MCP_RXM1SIDL    0x25
#define MCP_RXM1EID8    0x26
#define MCP_RXM1EID0    0x27

#define MCP_CNF3        0x28
#define MCP_CNF2        0x29
#define MCP_CNF1        0x2A
#define MCP_CANINTE     0x2B
#define MCP_CANINTF     0x2C
#define MCP_EFLG        0x2D

#define MCP_TXB0CTRL    0x30
#define MCP_TXB0SIDH    0x31
#define MCP_TXB0SIDL    0x32
#define MCP_TXB0EID8    0x33
#define MCP_TXB0EID0    0x34
#define MCP_TXB0DLC     0x35
#define MCP_TXB0D0      0x36
/* TXB0D1..D7 follow at 0x37..0x3D */

#define MCP_TXB1CTRL    0x40
#define MCP_TXB1SIDH    0x41
#define MCP_TXB1SIDL    0x42
#define MCP_TXB1EID8    0x43
#define MCP_TXB1EID0    0x44
#define MCP_TXB1DLC     0x45
#define MCP_TXB1D0      0x46

#define MCP_TXB2CTRL    0x50
#define MCP_TXB2SIDH    0x51
#define MCP_TXB2SIDL    0x52
#define MCP_TXB2EID8    0x53
#define MCP_TXB2EID0    0x54
#define MCP_TXB2DLC     0x55
#define MCP_TXB2D0      0x56

#define MCP_RXB0CTRL    0x60
#define MCP_RXB0SIDH    0x61
#define MCP_RXB0SIDL    0x62
#define MCP_RXB0EID8    0x63
#define MCP_RXB0EID0    0x64
#define MCP_RXB0DLC     0x65
#define MCP_RXB0D0      0x66

#define MCP_RXB1CTRL    0x70
#define MCP_RXB1SIDH    0x71
#define MCP_RXB1SIDL    0x72
#define MCP_RXB1EID8    0x73
#define MCP_RXB1EID0    0x74
#define MCP_RXB1DLC     0x75
#define MCP_RXB1D0      0x76

/* ----- CANCTRL (0x0F) -------------------------------------------------- */
#define MCP_CANCTRL_REQOP_MASK  0xE0
#define MCP_CANCTRL_REQOP_SHIFT 5
#define MCP_CANCTRL_ABAT        (1 << 4)
#define MCP_CANCTRL_OSM         (1 << 3)
#define MCP_CANCTRL_CLKEN       (1 << 2)
#define MCP_CANCTRL_CLKPRE_MASK 0x03

/* ----- CANSTAT (0x0E) -------------------------------------------------- */
#define MCP_CANSTAT_OPMOD_MASK  0xE0
#define MCP_CANSTAT_OPMOD_SHIFT 5
#define MCP_CANSTAT_ICOD_MASK   0x0E

/* Operating modes (REQOP bits 7..5 of CANCTRL; OPMOD bits 7..5 of CANSTAT) */
#define MCP_MODE_NORMAL         0x00
#define MCP_MODE_SLEEP          0x01
#define MCP_MODE_LOOPBACK       0x02
#define MCP_MODE_LISTENONLY     0x03
#define MCP_MODE_CONFIG         0x04

/* ----- CANINTE / CANINTF (interrupt enable / flag, identical bit layout) */
#define MCP_INT_RX0IF           (1 << 0)
#define MCP_INT_RX1IF           (1 << 1)
#define MCP_INT_TX0IF           (1 << 2)
#define MCP_INT_TX1IF           (1 << 3)
#define MCP_INT_TX2IF           (1 << 4)
#define MCP_INT_ERRIF           (1 << 5)
#define MCP_INT_WAKIF           (1 << 6)
#define MCP_INT_MERRF           (1 << 7)

/* ----- TXBnCTRL ------------------------------------------------------- */
#define MCP_TXBCTRL_TXP_MASK    0x03
#define MCP_TXBCTRL_TXREQ       (1 << 3)
#define MCP_TXBCTRL_TXERR       (1 << 4)
#define MCP_TXBCTRL_MLOA        (1 << 5)
#define MCP_TXBCTRL_ABTF        (1 << 6)

/* ----- RXBnCTRL ------------------------------------------------------- */
#define MCP_RXB0CTRL_FILHIT_MASK 0x01      /* RXB0: only bit 0 */
#define MCP_RXB0CTRL_BUKT        (1 << 2)  /* rollover to RXB1 */
#define MCP_RXB0CTRL_BUKT1       (1 << 1)  /* readonly copy of BUKT */
#define MCP_RXB0CTRL_RXRTR       (1 << 3)
#define MCP_RXB0CTRL_RXM_MASK    0x60      /* receive mode bits 6:5 */
#define MCP_RXB0CTRL_RXM_SHIFT   5

#define MCP_RXB1CTRL_FILHIT_MASK 0x07      /* RXB1: bits 0..2 */
#define MCP_RXB1CTRL_RXRTR       (1 << 3)
#define MCP_RXB1CTRL_RXM_MASK    0x60
#define MCP_RXB1CTRL_RXM_SHIFT   5

/* RXM modes (RXBnCTRL bits 6:5) */
#define MCP_RXM_RECV_ALL_FILTERED 0x00     /* normal: filters apply */
#define MCP_RXM_RECV_STD          0x01     /* standard frames only */
#define MCP_RXM_RECV_EXT          0x02     /* extended frames only */
#define MCP_RXM_RECV_ANY          0x03     /* receive any, no filtering */

/* ----- SIDL bits -------------------------------------------------------- */
#define MCP_SIDL_EXIDE          (1 << 3)   /* extended ID enable (TX/RX/filter) */
#define MCP_SIDL_SRR            (1 << 4)   /* substitute remote request (RX only) */

/* ----- DLC byte ------------------------------------------------------- */
#define MCP_DLC_DLC_MASK        0x0F
#define MCP_DLC_RTR             (1 << 6)

/* ----- EFLG bits ------------------------------------------------------- */
#define MCP_EFLG_EWARN          (1 << 0)
#define MCP_EFLG_RXWAR          (1 << 1)
#define MCP_EFLG_TXWAR          (1 << 2)
#define MCP_EFLG_RXEP           (1 << 3)
#define MCP_EFLG_TXEP           (1 << 4)
#define MCP_EFLG_TXBO           (1 << 5)
#define MCP_EFLG_RX0OVR         (1 << 6)
#define MCP_EFLG_RX1OVR         (1 << 7)

/* ----- SPI commands ---------------------------------------------------- */
#define MCP_SPI_RESET           0xC0
#define MCP_SPI_READ            0x03
#define MCP_SPI_READ_RX_BASE    0x90    /* 0x90, 0x92, 0x94, 0x96 */
#define MCP_SPI_WRITE           0x02
#define MCP_SPI_LOAD_TX_BASE    0x40    /* 0x40, 0x41, 0x42, 0x43, 0x44, 0x45 */
#define MCP_SPI_RTS_BASE        0x80    /* 0x80..0x87 (low 3 bits = TX buffer mask) */
#define MCP_SPI_READ_STATUS     0xA0
#define MCP_SPI_RX_STATUS       0xB0
#define MCP_SPI_BIT_MODIFY      0x05

#endif /* ANEB_MCP2515_REGS_H */
