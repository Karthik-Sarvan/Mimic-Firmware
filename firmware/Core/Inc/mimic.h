/**
  ******************************************************************************
  * @file    mimic.h
  * @brief   Mimic Command Interface Header
  * @author  Mimic Project
  ******************************************************************************
  * 
  * MIMIC - Hardware Peripheral Control via Commands
  * 
  * Supported Commands:
  *   GPIO: PIN_STATUS, PIN_SET_OUT, PIN_SET_IN, PIN_HIGH, PIN_LOW, PIN_READ
  *   UART: UART_INIT, UART_SEND, UART_RECV
  *   I2C:  (Coming soon)
  *   SPI:  (Coming soon)
  * 
  ******************************************************************************
  */

#ifndef __MIMIC_H__
#define __MIMIC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ========================== CONFIGURATION ================================= */

#define MIMIC_VERSION           "1.1.0"
#define MIMIC_MAX_CMD_LEN       128
#define MIMIC_MAX_ARGS          8
#define MIMIC_MAX_ARG_LEN       32

/* Host UART (for command interface) */
#define MIMIC_HOST_UART         huart1
#define MIMIC_HOST_BAUDRATE     115200

/* Response codes */
#define MIMIC_OK                "OK"
#define MIMIC_ERROR             "ERROR"
#define MIMIC_UNKNOWN           "UNKNOWN_CMD"

/* ========================== DATA STRUCTURES =============================== */

/* Command parser state */
typedef struct {
    char cmd_buffer[MIMIC_MAX_CMD_LEN];
    uint8_t cmd_index;
    uint8_t cmd_ready;
} Mimic_CmdState_t;

/* Parsed command structure */
typedef struct {
    char command[MIMIC_MAX_ARG_LEN];
    char args[MIMIC_MAX_ARGS][MIMIC_MAX_ARG_LEN];
    uint8_t argc;
} Mimic_Command_t;

/* GPIO Port mapping */
typedef struct {
    char name;
    GPIO_TypeDef *port;
} Mimic_PortMap_t;

/* SPI CS pin tracking */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t configured;
} Mimic_SPI_CS_t;

typedef struct {
    uint8_t is_slave;  // 0=Master, 1=Slave
} Mimic_I2C_State_t;

/* ========================== EXTERN VARIABLES ============================== */

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart6;

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;
extern SPI_HandleTypeDef hspi4;
extern SPI_HandleTypeDef hspi5;

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;

extern Mimic_CmdState_t mimic_state;

/* ========================== FUNCTION PROTOTYPES =========================== */

/* Core Functions */
void Mimic_Init(void);
void Mimic_Process(void);
void Mimic_SendResponse(const char *response);
void Mimic_SendResponseF(const char *format, ...);

/* Command Processing */
void Mimic_ProcessCommand(const char *cmd_line);
uint8_t Mimic_ParseCommand(const char *cmd_line, Mimic_Command_t *cmd);

/* GPIO Commands */
void Mimic_CMD_PIN_STATUS(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_SET_OUT(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_SET_IN(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_HIGH(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_LOW(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_READ(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_TOGGLE(Mimic_Command_t *cmd);
void Mimic_CMD_PIN_MODE(Mimic_Command_t *cmd);

/* UART Commands */
void Mimic_CMD_UART_INIT(Mimic_Command_t *cmd);
void Mimic_CMD_UART_SEND(Mimic_Command_t *cmd);
void Mimic_CMD_UART_RECV(Mimic_Command_t *cmd);
void Mimic_CMD_UART_STATUS(Mimic_Command_t *cmd);
void Mimic_CMD_UART_TEST(Mimic_Command_t *cmd);
void Mimic_CMD_UART_RS485(Mimic_Command_t *cmd);

/* SPI Commands */
void Mimic_CMD_SPI_INIT(Mimic_Command_t *cmd);
void Mimic_CMD_SPI_SEND(Mimic_Command_t *cmd);
void Mimic_CMD_SPI_RECV(Mimic_Command_t *cmd);
void Mimic_CMD_SPI_TRANSFER(Mimic_Command_t *cmd);
void Mimic_CMD_SPI_CS(Mimic_Command_t *cmd);
void Mimic_CMD_SPI_STATUS(Mimic_Command_t *cmd);

/* I2C Command Handlers */
void Mimic_CMD_I2C_INIT(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_SCAN(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_WRITE(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_READ(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_WRITE_READ(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_STATUS(Mimic_Command_t *cmd);

/* System Commands */
void Mimic_CMD_HELP(Mimic_Command_t *cmd);
void Mimic_CMD_VERSION(Mimic_Command_t *cmd);
void Mimic_CMD_STATUS(Mimic_Command_t *cmd);
void Mimic_CMD_RESET(Mimic_Command_t *cmd);

/* Helper Functions */
GPIO_TypeDef* Mimic_GetPort(char port_char);
uint16_t Mimic_GetPin(uint8_t pin_num);
uint8_t Mimic_ParsePin(const char *pin_str, GPIO_TypeDef **port, uint16_t *pin);

#ifdef __cplusplus
}
#endif

#endif /* __MIMIC_H__ */
