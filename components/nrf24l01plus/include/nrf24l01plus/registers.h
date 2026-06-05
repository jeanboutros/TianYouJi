#pragma once

/**
 * @file registers.h
 * @brief Umbrella include for all nRF24L01+ register definitions.
 *
 * @code
 *   #include <nrf24l01plus/registers.h>
 *   // Now all register structs, enums, and to_reg() functions are available.
 * @endcode
 */

#include "registers/addresses.h"
#include "registers/config.h"
#include "registers/en_aa.h"
#include "registers/en_rxaddr.h"
#include "registers/setup_aw.h"
#include "registers/setup_retr.h"
#include "registers/rf_ch.h"
#include "registers/rf_setup.h"
#include "registers/status.h"
#include "registers/observe_tx.h"
#include "registers/rpd.h"
#include "registers/rx_pw.h"
#include "registers/fifo_status.h"
#include "registers/dynpd.h"
#include "registers/feature.h"
