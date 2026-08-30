#include "mimic.h"

/**
  ******************************************************************************
  * @file    mimic.c
  * @brief   Mimic Command Interface Implementation
  * @author  Mimic Project
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "mimic.h"
#include "usart.h"
#include <stdarg.h>

/* ========================== PRIVATE VARIABLES ============================= */

Mimic_CmdState_t mimic_state;

/* Interrupt-based RX */
static volatile uint8_t rx_byte;
static volatile uint8_t rx_ready = 0;

/* Circular buffer for received data */
#define RX_BUFFER_SIZE 256
static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* Port mapping table */

/**
  * @brief  UART RX Complete Callback (called from interrupt)
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &MIMIC_HOST_UART)
    {
        /* Store in circular buffer */
        uint16_t next_head = (rx_head + 1) % RX_BUFFER_SIZE;
        if (next_head != rx_tail)  /* Buffer not full */
        {
            rx_buffer[rx_head] = rx_byte;
            rx_head = next_head;
        }
        
        /* Re-enable interrupt for next byte */
        HAL_UART_Receive_IT(&MIMIC_HOST_UART, (uint8_t*)&rx_byte, 1);
    }
}

/**
  * @brief  Check if data available in RX buffer
  */
static uint8_t Mimic_RxAvailable(void)
{
    return rx_head != rx_tail;
}

/**
  * @brief  Get one byte from RX buffer
  */
static uint8_t Mimic_RxGet(void)
{
    if (rx_head == rx_tail) return 0;
    uint8_t data = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
    return data;
}

/* ========================== CORE FUNCTIONS ================================ */

/**
  * @brief  Initialize Mimic command interface
  */
void Mimic_Init(void)
{
    // POWER UP ALL PINS IMMEDIATELY
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // Ensure LED pin is output
    GPIO_InitTypeDef GPIO_Led = {0};
    GPIO_Led.Pin = GPIO_PIN_13;
    GPIO_Led.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Led.Pull = GPIO_NOPULL;
    GPIO_Led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_Led);

    memset(&mimic_state, 0, sizeof(Mimic_CmdState_t));
    memset(i2c_register_map, 0, 256);
    
    // Seed standard sensor IDs for instant hardware handshake
    i2c_register_map[0x75] = 0x68; // MPU6050 WHO_AM_I
    i2c_register_map[0xD0] = 0x58; // BMP280 Chip ID
    
    /* Manual PC13 (LED) Init to be 100% sure */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* Start interrupt-based reception */
    HAL_UART_Receive_IT(&MIMIC_HOST_UART, (uint8_t*)&rx_byte, 1);
    
    /* Send welcome message */
    Mimic_SendResponse("\r\n");
    Mimic_SendResponse("MIMIC FIRMWARE STARTING...\r\n");
}

/**
  * @brief  Process incoming commands (call in main loop)
  */
void Mimic_Process(void)
{


    /* Process all available bytes from interrupt buffer */
    while (Mimic_RxAvailable())
    {
        uint8_t rx = Mimic_RxGet();
        
        /* Handle special characters */
        if (rx == '\r' || rx == '\n')
        {
            if (mimic_state.cmd_index > 0)
            {
                Mimic_SendResponse("\r\n");
                mimic_state.cmd_buffer[mimic_state.cmd_index] = '\0';
                Mimic_ProcessCommand(mimic_state.cmd_buffer);
                mimic_state.cmd_index = 0;
                Mimic_SendResponse("> ");
            }
        }
        else if (rx == '\b' || rx == 127)  /* Backspace */
        {
            if (mimic_state.cmd_index > 0)
            {
                mimic_state.cmd_index--;
                Mimic_SendResponse(" \b");  /* Erase character */
            }
        }
        else if (rx >= 32 && rx < 127)  /* Printable */
        {
            if (mimic_state.cmd_index < MIMIC_MAX_CMD_LEN - 1)
            {
                mimic_state.cmd_buffer[mimic_state.cmd_index++] = rx;
            }
        }
    }
    
    Mimic_SPI_Sync();
}

/**
  * @brief  Send response string
  */
void Mimic_SendResponse(const char *response)
{
    HAL_UART_Transmit(&MIMIC_HOST_UART, (uint8_t*)response, strlen(response), 1);
}

/**
  * @brief  Send formatted response
  */
void Mimic_SendResponseF(const char *format, ...)
{
    char buffer[MIMIC_MAX_CMD_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Mimic_SendResponse(buffer);
}

/* ========================== COMMAND PROCESSING ============================ */

/**
  * @brief  Parse command line into command structure
  */
uint8_t Mimic_ParseCommand(const char *cmd_line, Mimic_Command_t *cmd)
{
    char buffer[MIMIC_MAX_CMD_LEN];
    char *token;
    uint8_t idx = 0;
    
    memset(cmd, 0, sizeof(Mimic_Command_t));
    strncpy(buffer, cmd_line, MIMIC_MAX_CMD_LEN - 1);
    
    /* Get command */
    token = strtok(buffer, " \t");
    if (token == NULL) return 0;
    
    /* Convert to uppercase */
    for (int i = 0; token[i]; i++) {
        cmd->command[i] = (token[i] >= 'a' && token[i] <= 'z') ? token[i] - 32 : token[i];
    }
    
    /* Get arguments */
    while ((token = strtok(NULL, " \t")) != NULL && idx < MIMIC_MAX_ARGS)
    {
        strncpy(cmd->args[idx], token, MIMIC_MAX_ARG_LEN - 1);
        idx++;
    }
    cmd->argc = idx;
    
    return 1;
}

/* Forward declarations for command handlers */
void Mimic_CMD_UART_POLL(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_REG_SET(Mimic_Command_t *cmd);
void Mimic_CMD_I2C_REG_DATA(Mimic_Command_t *cmd);

/**
  * @brief  Process a command line
  */
void Mimic_ProcessCommand(const char *cmd_line)
{
    Mimic_Command_t cmd;
    
    if (!Mimic_ParseCommand(cmd_line, &cmd) || strlen(cmd.command) == 0)
    {
        return;
    }
    
    /* ===== GPIO Commands ===== */
    if (strcmp(cmd.command, "PIN_STATUS") == 0 || strcmp(cmd.command, "PS") == 0)
    {
        Mimic_CMD_PIN_STATUS(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_SET_OUT") == 0 || strcmp(cmd.command, "PSO") == 0)
    {
        Mimic_CMD_PIN_SET_OUT(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_SET_IN") == 0 || strcmp(cmd.command, "PSI") == 0)
    {
        Mimic_CMD_PIN_SET_IN(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_HIGH") == 0 || strcmp(cmd.command, "PH") == 0)
    {
        Mimic_CMD_PIN_HIGH(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_LOW") == 0 || strcmp(cmd.command, "PL") == 0)
    {
        Mimic_CMD_PIN_LOW(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_READ") == 0 || strcmp(cmd.command, "PR") == 0)
    {
        Mimic_CMD_PIN_READ(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_TOGGLE") == 0 || strcmp(cmd.command, "PT") == 0)
    {
        Mimic_CMD_PIN_TOGGLE(&cmd);
    }
    else if (strcmp(cmd.command, "PIN_MODE") == 0 || strcmp(cmd.command, "PM") == 0)
    {
        Mimic_CMD_PIN_MODE(&cmd);
    }
    /* ===== UART Commands ===== */
    else if (strcmp(cmd.command, "UART_INIT") == 0 || strcmp(cmd.command, "UI") == 0)
    {
        Mimic_CMD_UART_INIT(&cmd);
    }
    else if (strcmp(cmd.command, "UART_SEND") == 0 || strcmp(cmd.command, "US") == 0)
    {
        Mimic_CMD_UART_SEND(&cmd);
    }
    else if (strcmp(cmd.command, "UART_RECV") == 0 || strcmp(cmd.command, "UR") == 0)
    {
        Mimic_CMD_UART_RECV(&cmd);
    }
    else if (strcmp(cmd.command, "UART_STATUS") == 0)
    {
        Mimic_CMD_UART_STATUS(&cmd);
    }
    else if (strcmp(cmd.command, "UART_TEST") == 0)
    {
        Mimic_CMD_UART_TEST(&cmd);
    }
    else if (strcmp(cmd.command, "UART_RS485") == 0)
    {
        Mimic_CMD_UART_RS485(&cmd);
    }
    else if (strcmp(cmd.command, "UART_POLL") == 0 || strcmp(cmd.command, "UP") == 0)
    {
        Mimic_CMD_UART_POLL(&cmd);
    }
    /* ===== SPI Commands ===== */
    else if (strcmp(cmd.command, "SPI_INIT") == 0 || strcmp(cmd.command, "SI") == 0)
    {
        Mimic_CMD_SPI_INIT(&cmd);
    }
    else if (strcmp(cmd.command, "SPI_SEND") == 0 || strcmp(cmd.command, "SS") == 0)
    {
        Mimic_CMD_SPI_SEND(&cmd);
    }
    else if (strcmp(cmd.command, "SPI_RECV") == 0 || strcmp(cmd.command, "SR") == 0)
    {
        Mimic_CMD_SPI_RECV(&cmd);
    }
    else if (strcmp(cmd.command, "SPI_TRANSFER") == 0 || strcmp(cmd.command, "ST") == 0)
    {
        Mimic_CMD_SPI_TRANSFER(&cmd);
    }
    else if (strcmp(cmd.command, "SPI_CS") == 0 || strcmp(cmd.command, "SCS") == 0)
    {
        Mimic_CMD_SPI_CS(&cmd);
    }
    else if (strcmp(cmd.command, "SPI_STATUS") == 0)
    {
        Mimic_CMD_SPI_STATUS(&cmd);
    }
    /* ===== I2C Commands ===== */
    else if (strcmp(cmd.command, "I2C_INIT") == 0 || strcmp(cmd.command, "II") == 0)
    {
        Mimic_CMD_I2C_INIT(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_SCAN") == 0 || strcmp(cmd.command, "IS") == 0)
    {
        Mimic_CMD_I2C_SCAN(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_WRITE") == 0 || strcmp(cmd.command, "IW") == 0)
    {
        Mimic_CMD_I2C_WRITE(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_READ") == 0 || strcmp(cmd.command, "IR") == 0)
    {
        Mimic_CMD_I2C_READ(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_WRITE_READ") == 0 || strcmp(cmd.command, "IWR") == 0)
    {
        Mimic_CMD_I2C_WRITE_READ(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_REG_SET") == 0 || strcmp(cmd.command, "IRS") == 0)
    {
        Mimic_CMD_I2C_REG_SET(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_REG_DATA") == 0 || strcmp(cmd.command, "IRD") == 0)
    {
        Mimic_CMD_I2C_REG_DATA(&cmd);
    }
    else if (strcmp(cmd.command, "I2C_STATUS") == 0)
    {
        Mimic_CMD_I2C_STATUS(&cmd);
    }
    /* ===== System Commands ===== */
    else if (strcmp(cmd.command, "HELP") == 0 || strcmp(cmd.command, "?") == 0)
    {
        Mimic_CMD_HELP(&cmd);
    }
    else if (strcmp(cmd.command, "VERSION") == 0 || strcmp(cmd.command, "VER") == 0)
    {
        Mimic_CMD_VERSION(&cmd);
    }
    else if (strcmp(cmd.command, "STATUS") == 0)
    {
        Mimic_CMD_STATUS(&cmd);
    }
    else if (strcmp(cmd.command, "RESET") == 0)
    {
        Mimic_CMD_RESET(&cmd);
    }
    else
    {
        Mimic_SendResponseF("ERROR: Unknown command '%s'\r\n", cmd.command);
    }
}

