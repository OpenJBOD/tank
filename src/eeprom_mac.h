/*
 * EEPROM MAC Address Driver for OpenJBOD
 */

#ifndef EEPROM_MAC_H
#define EEPROM_MAC_H

#include <stdint.h>

/**
 * @brief Read MAC address from EEPROM
 * 
 * @param mac_addr Buffer to store the 6-byte MAC address
 * @return 0 on success, negative error code on failure
 */
int read_mac_address(uint8_t *mac_addr);

#endif /* EEPROM_MAC_H */