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
static const Mimic_PortMap_t port_map[] = {
    {'A', GPIOA},
    {'B', GPIOB},
    {'C', GPIOC},
    {'H', GPIOH},
    {0, NULL}
};

/* UART handles for dynamic configuration are handled in usart.c */

/* UART RS485 DE pin tracking */
typedef struct {
    GPIO_TypeDef *de_port;
    uint16_t de_pin;
    uint8_t de_active_high;
    uint8_t configured;
} Mimic_UART_RS485_t;

static Mimic_UART_RS485_t uart_rs485_config[3] = {0}; // Index: 0=USART1, 1=USART2, 2=USART6

/* SPI handles for dynamic configuration */
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi3;
SPI_HandleTypeDef hspi4;
SPI_HandleTypeDef hspi5;

/* SPI CS pin tracking */
static Mimic_SPI_CS_t spi_cs_pins[5] = {0};  // One for each SPI instance

/* I2C handles for dynamic configuration */
I2C_HandleTypeDef hi2c1;
void Mimic_SPI_Sync(void);
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;

/* I2C State tracking */
static Mimic_I2C_State_t i2c_states[3] = {0}; // 0=I2C1, 1=I2C2, 2=I2C3
static uint8_t i2c_slave_rx_buffer[64];

/* Shadow Register Map for fast sensor emulation */
static uint8_t i2c_register_map[256] = {0};
static uint8_t current_reg_addr = 0;

/* SPI Slave Buffers */
static uint8_t spi_tx_buf[2] = {0};
static uint8_t spi_rx_buf[2] = {0};
volatile uint8_t spi_state = 0;
volatile uint8_t spi_active = 0;

/* ========================== INTERRUPT HANDLERS ============================ */

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

/* ========================== HELPER FUNCTIONS ============================== */

/**
  * @brief  Get GPIO port from character
  */
GPIO_TypeDef* Mimic_GetPort(char port_char)
{
    char upper = (port_char >= 'a' && port_char <= 'z') ? port_char - 32 : port_char;
    
    for (int i = 0; port_map[i].port != NULL; i++)
    {
        if (port_map[i].name == upper)
        {
            return port_map[i].port;
        }
    }
    return NULL;
}

/**
  * @brief  Get GPIO pin mask from number
  */
uint16_t Mimic_GetPin(uint8_t pin_num)
{
    if (pin_num > 15) return 0;
    return (1 << pin_num);
}

/**
  * @brief  Parse pin string like "A5" or "D12"
  */
uint8_t Mimic_ParsePin(const char *pin_str, GPIO_TypeDef **port, uint16_t *pin)
{
    if (strlen(pin_str) < 2) return 0;
    
    // Support "PA4" style by skipping the 'P' prefix if present
    const char *ptr = pin_str;
    if ((*ptr == 'P' || *ptr == 'p') && strlen(ptr) >= 3) {
        ptr++;
    }
    
    *port = Mimic_GetPort(*ptr);
    if (*port == NULL) return 0;
    
    uint8_t pin_num = atoi(ptr + 1);
    *pin = Mimic_GetPin(pin_num);
    if (*pin == 0 && pin_str[1] != '0') return 0;
    
    return 1;
}

/* ========================== GPIO COMMANDS ================================= */

/**
  * @brief  PIN_STATUS <PIN> - Show pin configuration
  */
void Mimic_CMD_PIN_STATUS(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_STATUS <PIN> (e.g., PIN_STATUS A5)\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    uint8_t pin_num = 0;
    for (int i = 0; i < 16; i++) {
        if (pin & (1 << i)) { pin_num = i; break; }
    }
    
    /* Read MODER */
    uint32_t moder = (port->MODER >> (pin_num * 2)) & 0x03;
    const char *mode_str[] = {"INPUT", "OUTPUT", "ALT_FUNC", "ANALOG"};
    
    /* Read OTYPER */
    uint32_t otyper = (port->OTYPER >> pin_num) & 0x01;
    const char *otype_str[] = {"PUSH_PULL", "OPEN_DRAIN"};
    
    /* Read OSPEEDR */
    uint32_t ospeedr = (port->OSPEEDR >> (pin_num * 2)) & 0x03;
    const char *speed_str[] = {"LOW", "MEDIUM", "HIGH", "VERY_HIGH"};
    
    /* Read PUPDR */
    uint32_t pupdr = (port->PUPDR >> (pin_num * 2)) & 0x03;
    const char *pupd_str[] = {"NONE", "PULL_UP", "PULL_DOWN", "RESERVED"};
    
    /* Read current state */
    uint8_t state = HAL_GPIO_ReadPin(port, pin);
    
    Mimic_SendResponseF("PIN %s:\r\n", cmd->args[0]);
    Mimic_SendResponseF("  Mode:   %s\r\n", mode_str[moder]);
    Mimic_SendResponseF("  Type:   %s\r\n", otype_str[otyper]);
    Mimic_SendResponseF("  Speed:  %s\r\n", speed_str[ospeedr]);
    Mimic_SendResponseF("  Pull:   %s\r\n", pupd_str[pupdr]);
    Mimic_SendResponseF("  State:  %s\r\n", state ? "HIGH" : "LOW");
}

/**
  * @brief  PIN_SET_OUT <PIN> - Configure pin as output
  */
void Mimic_CMD_PIN_SET_OUT(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_SET_OUT <PIN>\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    /* Enable GPIO clock */
    if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
    
    Mimic_SendResponseF("OK: %s configured as OUTPUT\r\n", cmd->args[0]);
}

/**
  * @brief  PIN_SET_IN <PIN> [PULL] - Configure pin as input
  */
void Mimic_CMD_PIN_SET_IN(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_SET_IN <PIN> [UP|DOWN|NONE]\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    /* Enable GPIO clock */
    if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    
    uint32_t pull = GPIO_NOPULL;
    if (cmd->argc >= 2)
    {
        if (strcmp(cmd->args[1], "UP") == 0) pull = GPIO_PULLUP;
        else if (strcmp(cmd->args[1], "DOWN") == 0) pull = GPIO_PULLDOWN;
    }
    
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = pull;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
    
    Mimic_SendResponseF("OK: %s configured as INPUT\r\n", cmd->args[0]);
}

/**
  * @brief  PIN_HIGH <PIN> - Set pin high
  */
void Mimic_CMD_PIN_HIGH(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_HIGH <PIN>\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    Mimic_SendResponseF("OK: %s = HIGH\r\n", cmd->args[0]);
}

/**
  * @brief  PIN_LOW <PIN> - Set pin low
  */
void Mimic_CMD_PIN_LOW(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_LOW <PIN>\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    Mimic_SendResponseF("OK: %s = LOW\r\n", cmd->args[0]);
}

/**
  * @brief  PIN_READ <PIN> - Read pin state
  */
void Mimic_CMD_PIN_READ(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_READ <PIN>\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    uint8_t state = HAL_GPIO_ReadPin(port, pin);
    Mimic_SendResponseF("%s = %s\r\n", cmd->args[0], state ? "HIGH" : "LOW");
}

/**
  * @brief  PIN_TOGGLE <PIN> - Toggle pin state
  */
void Mimic_CMD_PIN_TOGGLE(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: PIN_TOGGLE <PIN>\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    HAL_GPIO_TogglePin(port, pin);
    uint8_t state = HAL_GPIO_ReadPin(port, pin);
    Mimic_SendResponseF("OK: %s toggled to %s\r\n", cmd->args[0], state ? "HIGH" : "LOW");
}

/**
  * @brief  PIN_MODE <PIN> <MODE> - Set pin mode
  */
void Mimic_CMD_PIN_MODE(Mimic_Command_t *cmd)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: PIN_MODE <PIN> <IN|OUT|AF|AN>\r\n");
        return;
    }
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    /* Enable GPIO clock */
    if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    
    if (strcmp(cmd->args[1], "IN") == 0)
    {
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    }
    else if (strcmp(cmd->args[1], "OUT") == 0)
    {
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    }
    else if (strcmp(cmd->args[1], "AF") == 0)
    {
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    }
    else if (strcmp(cmd->args[1], "AN") == 0)
    {
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    }
    else
    {
        Mimic_SendResponse("ERROR: Mode must be IN, OUT, AF, or AN\r\n");
        return;
    }
    
    HAL_GPIO_Init(port, &GPIO_InitStruct);
    Mimic_SendResponseF("OK: %s mode set to %s\r\n", cmd->args[0], cmd->args[1]);
}

/* ========================== UART COMMANDS ================================= */

/**
  * @brief  Get UART handle from instance name
  */
static UART_HandleTypeDef* Mimic_GetUARTHandle(const char *instance)
{
    if (strcmp(instance, "UART2") == 0 || strcmp(instance, "USART2") == 0 || strcmp(instance, "2") == 0)
        return &huart2;
    else if (strcmp(instance, "UART6") == 0 || strcmp(instance, "USART6") == 0 || strcmp(instance, "6") == 0)
        return &huart6;
    return NULL;  /* UART1 is reserved for host */
}

/**
  * @brief  Configure UART GPIO pins
  */
static void Mimic_ConfigureUARTGPIO(USART_TypeDef *instance)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    if (instance == USART1)
    {
        /* USART1: PA9 (TX), PA10 (RX) */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
    else if (instance == USART2)
    {
        /* USART2: PA2 (TX), PA3 (RX) */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
    else if (instance == USART6)
    {
        /* USART6: PA11 (TX), PA12 (RX) */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

/**
  * @brief  UART_INIT <INSTANCE> <BAUDRATE> [PARITY] [STOPBITS]
  */
void Mimic_CMD_UART_INIT(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: UART_INIT <1|6> <BAUDRATE> [N|E|O] [1|2]\r\n");
        Mimic_SendResponse("  Example: UART_INIT 1 9600 N 1\r\n");
        return;
    }
    
    UART_HandleTypeDef *huart = Mimic_GetUARTHandle(cmd->args[0]);
    if (huart == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid UART (use 1 or 6, UART2 is reserved)\r\n");
        return;
    }
    
    uint32_t baudrate = atoi(cmd->args[1]);
    if (baudrate < 300 || baudrate > 3000000)
    {
        Mimic_SendResponse("ERROR: Invalid baudrate (300-3000000)\r\n");
        return;
    }
    
    /* Enable UART clock */
    if (huart == &huart1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        huart->Instance = USART1;
        Mimic_ConfigureUARTGPIO(USART1);
    }
    else if (huart == &huart6)
    {
        __HAL_RCC_USART6_CLK_ENABLE();
        huart->Instance = USART6;
        Mimic_ConfigureUARTGPIO(USART6);
    }
    
    huart->Init.BaudRate = baudrate;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits = UART_STOPBITS_1;
    huart->Init.Parity = UART_PARITY_NONE;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    
    /* Parse parity */
    if (cmd->argc >= 3)
    {
        if (cmd->args[2][0] == 'E' || cmd->args[2][0] == 'e')
        {
            huart->Init.Parity = UART_PARITY_EVEN;
            huart->Init.WordLength = UART_WORDLENGTH_9B;
        }
        else if (cmd->args[2][0] == 'O' || cmd->args[2][0] == 'o')
        {
            huart->Init.Parity = UART_PARITY_ODD;
            huart->Init.WordLength = UART_WORDLENGTH_9B;
        }
    }
    
    /* Parse stop bits */
    if (cmd->argc >= 4)
    {
        if (cmd->args[3][0] == '2')
        {
            huart->Init.StopBits = UART_STOPBITS_2;
        }
    }
    
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        Mimic_SendResponse("ERROR: UART init failed\r\n");
        return;
    }
    
    /* Reset state to ready */
    huart->gState = HAL_UART_STATE_READY;
    huart->RxState = HAL_UART_STATE_READY;
    
    /* Verify actual baud rate */
    uint32_t pclk;
    if (huart->Instance == USART1 || huart->Instance == USART6)
    {
        pclk = HAL_RCC_GetPCLK2Freq();
    }
    else
    {
        pclk = HAL_RCC_GetPCLK1Freq();
    }
    
    Mimic_SendResponseF("OK: UART%s initialized\r\n", cmd->args[0]);
    Mimic_SendResponseF("  Requested: %lu baud\r\n", baudrate);
    Mimic_SendResponseF("  PCLK: %lu Hz, BRR: %lu\r\n", pclk, huart->Instance->BRR);
}

/**
  * @brief  UART_RS485 <INSTANCE> <DE_PIN> [ACTIVE_HIGH=1]
  */
void Mimic_CMD_UART_RS485(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: UART_RS485 <2|6> <PIN> [1|0]\r\n");
        Mimic_SendResponse("  Example: UART_RS485 6 A11 1\r\n");
        return;
    }
    
    UART_HandleTypeDef *huart = Mimic_GetUARTHandle(cmd->args[0]);
    if (huart == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid UART instance\r\n");
        return;
    }
    
    uint8_t inst_idx = (huart->Instance == USART1) ? 0 : (huart->Instance == USART2) ? 1 : 2;
    
    GPIO_TypeDef *port;
    uint16_t pin;
    if (!Mimic_ParsePin(cmd->args[1], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid DE pin '%s'\r\n", cmd->args[1]);
        return;
    }
    
    uart_rs485_config[inst_idx].de_port = port;
    uart_rs485_config[inst_idx].de_pin = pin;
    uart_rs485_config[inst_idx].de_active_high = (cmd->argc >= 3) ? atoi(cmd->args[2]) : 1;
    uart_rs485_config[inst_idx].configured = 1;
    
    /* Configure DE pin as output */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
    
    /* Set to inactive state */
    HAL_GPIO_WritePin(port, pin, uart_rs485_config[inst_idx].de_active_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
    
    Mimic_SendResponseF("OK: UART%s RS485 DE pin set to %s\r\n", cmd->args[0], cmd->args[1]);
}

/**
  * @brief  UART_SEND <INSTANCE> <DATA>
  */
void Mimic_CMD_UART_SEND(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: UART_SEND <1|6> <DATA>\r\n");
        return;
    }
    
    UART_HandleTypeDef *huart = Mimic_GetUARTHandle(cmd->args[0]);
    if (huart == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid UART instance\r\n");
        return;
    }
    
    if (huart->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: UART not initialized. Use UART_INIT first.\r\n");
        return;
    }
    
    /* Combine all remaining args as data */
    char data[MIMIC_MAX_CMD_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++)
    {
        if (i > 1) strcat(data, " ");
        strcat(data, cmd->args[i]);
    }
    
    /* Handle escape sequences */
    char *ptr = data;
    char output[MIMIC_MAX_CMD_LEN];
    int out_idx = 0;
    
    while (*ptr && out_idx < MIMIC_MAX_CMD_LEN - 1)
    {
        if (*ptr == '\\' && *(ptr+1))
        {
            ptr++;
            switch (*ptr)
            {
                case 'n': output[out_idx++] = '\n'; break;
                case 'r': output[out_idx++] = '\r'; break;
                case 't': output[out_idx++] = '\t'; break;
                case '\\': output[out_idx++] = '\\'; break;
                default: output[out_idx++] = *ptr; break;
            }
        }
        else
        {
            output[out_idx++] = *ptr;
        }
        ptr++;
    }
    output[out_idx] = '\0';
    
    uint8_t inst_idx = (huart->Instance == USART1) ? 0 : (huart->Instance == USART2) ? 1 : 2;
    if (uart_rs485_config[inst_idx].configured) {
        HAL_GPIO_WritePin(uart_rs485_config[inst_idx].de_port, uart_rs485_config[inst_idx].de_pin, 
                         uart_rs485_config[inst_idx].de_active_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    
    HAL_StatusTypeDef status = HAL_UART_Transmit(huart, (uint8_t*)output, out_idx, 1000);
    
    if (uart_rs485_config[inst_idx].configured) {
        /* Wait for transmission complete before disabling DE */
        while (__HAL_UART_GET_FLAG(huart, UART_FLAG_TC) == RESET);
        HAL_GPIO_WritePin(uart_rs485_config[inst_idx].de_port, uart_rs485_config[inst_idx].de_pin, 
                         uart_rs485_config[inst_idx].de_active_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
    
    if (status == HAL_OK)
    {
        Mimic_SendResponseF("OK: Sent %d bytes\r\n", out_idx);
    }
    else
    {
        Mimic_SendResponse("ERROR: Transmit failed\r\n");
    }
}

/**
  * @brief  UART_RECV <INSTANCE> <LENGTH> [TIMEOUT_MS]
  */
void Mimic_CMD_UART_RECV(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: UART_RECV <1|6> <LENGTH> [TIMEOUT_MS]\r\n");
        return;
    }
    
    UART_HandleTypeDef *huart = Mimic_GetUARTHandle(cmd->args[0]);
    if (huart == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid UART instance\r\n");
        return;
    }
    
    if (huart->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: UART not initialized\r\n");
        return;
    }
    
    /* Check and clear any error state */
    if (huart->gState != HAL_UART_STATE_READY)
    {
        Mimic_SendResponseF("WARNING: UART state=0x%02X, resetting...\r\n", huart->gState);
        huart->gState = HAL_UART_STATE_READY;
        huart->RxState = HAL_UART_STATE_READY;
    }
    
    /* Clear any pending errors */
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    
    uint16_t length = atoi(cmd->args[1]);
    if (length > 64) length = 64;
    
    uint32_t timeout = 1000;
    if (cmd->argc >= 3)
    {
        timeout = atoi(cmd->args[2]);
    }
    
    /* Send status and wait for TX to complete before blocking on RX */
    Mimic_SendResponseF("Waiting for %d bytes (timeout: %lu ms)...\r\n", length, timeout);
    
    /* Wait for UART2 TX to complete so message is sent before we block */
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {}
    
    /* Debug: Check UART state before receive */
    Mimic_SendResponseF("DEBUG: gState=0x%02X, RxState=0x%02X, ErrorCode=0x%02lX\r\n", 
                        huart->gState, huart->RxState, huart->ErrorCode);
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {}
    
    /* Live Polling Strategy:
       1. Loop until 'length' bytes received OR 'timeout' expires.
       2. Read byte-by-byte with a short timeout to check for data.
       3. Echo received characters immediately ("Live Polling").
       4. Return full/partial buffer at the end.
    */
    
    uint8_t buffer[65] = {0};
    uint16_t rx_count = 0;
    uint32_t start_time = HAL_GetTick();
    
    Mimic_SendResponse("LIVE: "); // Prefix for live stream
    
    while (rx_count < length)
    {
        /* Check for total timeout */
        if ((HAL_GetTick() - start_time) > timeout)
        {
            break;
        }
        
        /* Try to read 1 byte with short timeout (10ms) 
           10ms allows checking 'timeout' frequently enough 
        */
        uint8_t byte = 0;
        if (HAL_UART_Receive(huart, &byte, 1, 10) == HAL_OK)
        {
            buffer[rx_count++] = byte;
            
            /* Live Echo: Print char if printable, else dot */
            if (byte >= 32 && byte <= 126)
                Mimic_SendResponseF("%c", byte);
            else if (byte == '\r' || byte == '\n')
                 Mimic_SendResponseF("%c", byte); // Pass newlines
            else
                Mimic_SendResponse(".");
        }
    }
    
    Mimic_SendResponse("\r\n"); // End live line
    
    uint32_t elapsed = HAL_GetTick() - start_time;
    
    /* Output Final Summary */
    if (rx_count > 0)
    {
        buffer[rx_count] = '\0';
        
        if (rx_count < length)
             Mimic_SendResponseF("PARTIAL (Time: %lu ms, %d/%d bytes): ", elapsed, rx_count, length);
        else
             Mimic_SendResponseF("DONE (Time: %lu ms, %d/%d bytes): ", elapsed, rx_count, length);
             
        Mimic_SendResponse("DATA: ");
        Mimic_SendResponse((char*)buffer);
        Mimic_SendResponse("\r\n");
        
        /* Also show hex */
        Mimic_SendResponse("HEX: ");
        for (int i = 0; i < rx_count; i++)
        {
            Mimic_SendResponseF("%02X ", buffer[i]);
        }
        Mimic_SendResponse("\r\n");
    }
    else
    {
        Mimic_SendResponse("TIMEOUT: No data received\r\n");
    }
}

/**
  * @brief  UART_STATUS - Show UART status
  */
void Mimic_CMD_UART_STATUS(Mimic_Command_t *cmd)
{
    Mimic_SendResponse("UART Status:\r\n");
    Mimic_SendResponseF("  UART2 (Host): %lu baud (ACTIVE)\r\n", huart2.Init.BaudRate);
    
    if (huart1.Instance != NULL)
    {
        Mimic_SendResponseF("  UART1: %lu baud (PA9/PA10)\r\n", huart1.Init.BaudRate);
    }
    else
    {
        Mimic_SendResponse("  UART1: Not initialized\r\n");
    }
}

/**
  * @brief  UART_TEST <INSTANCE> - Loopback test (TX->RX wire required)
  */
void Mimic_CMD_UART_TEST(Mimic_Command_t *cmd)
{
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: UART_TEST <1|6>\r\n");
        Mimic_SendResponse("  Requires TX->RX loopback wire!\r\n");
        return;
    }
    
    UART_HandleTypeDef *huart = Mimic_GetUARTHandle(cmd->args[0]);
    if (huart == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid UART instance\r\n");
        return;
    }
    
    if (huart->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: UART not initialized. Use UART_INIT first.\r\n");
        return;
    }
    
    const char *test_msg = "MIMIC_LOOPBACK_TEST";
    uint8_t rx_buf[32] = {0};
    uint8_t len = strlen(test_msg);
    
    Mimic_SendResponseF("Testing UART%s loopback...\r\n", cmd->args[0]);
    Mimic_SendResponseF("  TX: \"%s\" (%d bytes)\r\n", test_msg, len);
    
    /* Send test message */
    HAL_StatusTypeDef tx_status = HAL_UART_Transmit(huart, (uint8_t*)test_msg, len, 100);
    if (tx_status != HAL_OK)
    {
        Mimic_SendResponse("  TX FAILED!\r\n");
        return;
    }
    
    /* Receive with timeout */
    HAL_StatusTypeDef rx_status = HAL_UART_Receive(huart, rx_buf, len, 500);
    
    if (rx_status == HAL_OK)
    {
        rx_buf[len] = '\0';
        Mimic_SendResponseF("  RX: \"%s\" (%d bytes)\r\n", rx_buf, len);
        
        if (memcmp(test_msg, rx_buf, len) == 0)
        {
            Mimic_SendResponse("  RESULT: PASS - Loopback OK!\r\n");
        }
        else
        {
            Mimic_SendResponse("  RESULT: FAIL - Data mismatch!\r\n");
        }
    }
    else if (rx_status == HAL_TIMEOUT)
    {
        Mimic_SendResponse("  RX: TIMEOUT - No data received\r\n");
        Mimic_SendResponse("  RESULT: FAIL - Check TX->RX wire!\r\n");
    }
    else
    {
        Mimic_SendResponse("  RX: ERROR\r\n");
        Mimic_SendResponse("  RESULT: FAIL\r\n");
    }
}

/* ========================== SPI COMMANDS ================================== */

/**
  * @brief  Get SPI handle from instance name
  */
static SPI_HandleTypeDef* Mimic_GetSPIHandle(const char *instance)
{
    if (strcmp(instance, "SPI1") == 0 || strcmp(instance, "1") == 0)
        return &hspi1;
    else if (strcmp(instance, "SPI2") == 0 || strcmp(instance, "2") == 0)
        return &hspi2;
    else if (strcmp(instance, "SPI3") == 0 || strcmp(instance, "3") == 0)
        return &hspi3;
    else if (strcmp(instance, "SPI4") == 0 || strcmp(instance, "4") == 0)
        return &hspi4;
    else if (strcmp(instance, "SPI5") == 0 || strcmp(instance, "5") == 0)
        return &hspi5;
    return NULL;
}

/**
  * @brief  Configure SPI GPIO pins
  */
static void Mimic_ConfigureSPIGPIO(SPI_TypeDef *instance)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    if (instance == SPI1)
    {
        /* SPI1: PA4 (NSS), PA5 (SCK), PA6 (MISO), PA7 (MOSI) */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
    else if (instance == SPI5)
    {
        /* SPI5: PE12 (SCK), PE13 (MISO), PE14 (MOSI) */
        __HAL_RCC_GPIOE_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF6_SPI4;  // SPI5 uses AF6
        HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    }
}

/**
  * @brief  Parse hex string to byte array
  */
static uint16_t Mimic_ParseHexData(const char *hex_str, uint8_t *data, uint16_t max_len)
{
    uint16_t count = 0;
    const char *ptr = hex_str;
    
    while (*ptr && count < max_len)
    {
        // Skip whitespace
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (!*ptr) break;
        
        // Parse hex byte
        char hex_byte[3] = {0};
        if (*ptr && ((*(ptr+1) >= '0' && *(ptr+1) <= '9') || 
                     (*(ptr+1) >= 'A' && *(ptr+1) <= 'F') ||
                     (*(ptr+1) >= 'a' && *(ptr+1) <= 'f')))
        {
            hex_byte[0] = *ptr++;
            hex_byte[1] = *ptr++;
        }
        else
        {
            hex_byte[0] = '0';
            hex_byte[1] = *ptr++;
        }
        
        data[count++] = (uint8_t)strtol(hex_byte, NULL, 16);
    }
    
    return count;
}

/**
  * @brief  SPI_INIT <INSTANCE> <MODE> <SPEED> [CPOL] [CPHA] [DATASIZE] [BITORDER] [CS_PIN]
  */
void Mimic_CMD_SPI_INIT(Mimic_Command_t *cmd)
{
    if (cmd->argc < 3)
    {
        Mimic_SendResponse("Usage: SPI_INIT <1-5> <MASTER|SLAVE> <SPEED> [CPOL] [CPHA] [DATASIZE] [BITORDER] [CS_PIN]\r\n");
        Mimic_SendResponse("  Example: SPI_INIT 1 MASTER 1000000\r\n");
        Mimic_SendResponse("  Example: SPI_INIT 1 MASTER 1000000 0 0 8 MSB A4\r\n");
        Mimic_SendResponse("  Example: SPI_INIT 2 MASTER 500000 1 1 8 MSB B12\r\n");
        Mimic_SendResponse("  CPOL: 0|1 (default 0), CPHA: 0|1 (default 0)\r\n");
        Mimic_SendResponse("  DATASIZE: 8|16 (default 8), BITORDER: MSB|LSB (default MSB)\r\n");
        Mimic_SendResponse("  CS_PIN: GPIO pin for automatic CS control (e.g., A4, B12)\r\n");
        return;
    }
    
    SPI_HandleTypeDef *hspi = Mimic_GetSPIHandle(cmd->args[0]);
    if (hspi == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid SPI instance (use 1-5)\r\n");
        return;
    }
    
    // Get SPI instance index (0-4)
    uint8_t spi_idx = atoi(cmd->args[0]) - 1;
    
    uint32_t speed = atoi(cmd->args[2]);
    if (speed < 100 || speed > 50000000)
    {
        Mimic_SendResponse("ERROR: Invalid speed (100-50000000 Hz)\r\n");
        return;
    }
    
    /* Enable SPI clock */
    if (hspi == &hspi1)
    {
        __HAL_RCC_SPI1_CLK_ENABLE();
        hspi->Instance = SPI1;
        Mimic_ConfigureSPIGPIO(SPI1);
    }
    else if (hspi == &hspi2)
    {
        __HAL_RCC_SPI2_CLK_ENABLE();
        hspi->Instance = SPI2;
        Mimic_ConfigureSPIGPIO(SPI2);
    }
    else if (hspi == &hspi3)
    {
        __HAL_RCC_SPI3_CLK_ENABLE();
        hspi->Instance = SPI3;
        Mimic_ConfigureSPIGPIO(SPI3);
    }
    else if (hspi == &hspi4)
    {
        __HAL_RCC_SPI4_CLK_ENABLE();
        hspi->Instance = SPI4;
        Mimic_ConfigureSPIGPIO(SPI4);
    }
    else if (hspi == &hspi5)
    {
        __HAL_RCC_SPI5_CLK_ENABLE();
        hspi->Instance = SPI5;
        Mimic_ConfigureSPIGPIO(SPI5);
    }
    
    /* SAFETY CHECK: Ensure we have at least 2 arguments (SPI_INIT + MODE) */
    if (cmd->argc < 2 || cmd->args[1][0] == '\0')
    {
        Mimic_SendResponse("ERROR: Missing Mode (MASTER/SLAVE)\r\n");
        return;
    }
    
    /* Configure SPI parameters */
    if (strcmp(cmd->args[1], "SLAVE") == 0)
    {
        /* HARDWARE SPI1 SLAVE CONFIG (SUPER FAST) */
        if (hspi->Instance == SPI1)
        {
            __HAL_RCC_SPI1_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            
            GPIO_InitTypeDef GPIO_Init = {0};
            // SCK(PA5), MISO(PA6), MOSI(PA7)
            GPIO_Init.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
            GPIO_Init.Mode = GPIO_MODE_AF_PP;
            GPIO_Init.Pull = GPIO_NOPULL;
            GPIO_Init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
            GPIO_Init.Alternate = GPIO_AF5_SPI1;
            HAL_GPIO_Init(GPIOA, &GPIO_Init);
            
            // NSS(PA4)
            GPIO_Init.Pin = GPIO_PIN_4;
            GPIO_Init.Mode = GPIO_MODE_AF_PP;
            GPIO_Init.Alternate = GPIO_AF5_SPI1;
            HAL_GPIO_Init(GPIOA, &GPIO_Init);

            hspi->Instance = SPI1;
            hspi->Init.Mode = SPI_MODE_SLAVE;
            hspi->Init.Direction = SPI_DIRECTION_2LINES;
            hspi->Init.DataSize = SPI_DATASIZE_8BIT;
            hspi->Init.CLKPolarity = SPI_POLARITY_LOW;
            hspi->Init.CLKPhase = SPI_PHASE_1EDGE;
            hspi->Init.NSS = SPI_NSS_HARD_INPUT;
            hspi->Init.FirstBit = SPI_FIRSTBIT_MSB;
            HAL_SPI_Init(hspi);

            /* BMP280 Calibration Data (Mapped to 7-bit SPI addresses) */
            // Chip ID at 0xD0 (7-bit 0x50)
            i2c_register_map[0x50] = 0x58;
            
            // Calibration T1-T3 (0x88-0x8D -> 0x08-0x0D)
            // Calibration P1-P9 (0x8E-0x9F -> 0x0E-0x1F)
            uint8_t cal[] = {
                0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, // T1-T3
                0x7D, 0x8D, 0x4B, 0xD6, 0xD0, 0x0B, // P1-P3
                0x27, 0x0B, 0xF9, 0xFF, 0x8C, 0x3C, // P4-P6
                0xF8, 0xF9, 0xAC, 0x26, 0x0A, 0xD1  // P7-P9
            };
            for(int i=0; i<24; i++) i2c_register_map[0x08+i] = cal[i];

            // Data Registers (0xF7-0xFC -> 0x77-0x7C)
            i2c_register_map[0x77] = 0x50; i2c_register_map[0x78] = 0xC3; i2c_register_map[0x79] = 0x00; // Pressure
            i2c_register_map[0x7A] = 0x80; i2c_register_map[0x7B] = 0x00; i2c_register_map[0x7C] = 0x00; // Temp
            
            spi_active = 1;
            
            HAL_NVIC_SetPriority(SPI1_IRQn, 5, 0); 
            HAL_NVIC_EnableIRQ(SPI1_IRQn);
            HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0); 
            HAL_NVIC_EnableIRQ(EXTI4_IRQn);

            // Chamber the ID immediately for zero latency
            spi_tx_buf[0] = 0x58;
            HAL_SPI_TransmitReceive_IT(hspi, (uint8_t*)&spi_tx_buf[0], (uint8_t*)&spi_rx_buf[0], 1);
            Mimic_SendResponse("OK: SPI1 SLAVE MODE 0 READY\r\n");
            return;
        }
        else
        {
            hspi->Init.Mode = SPI_MODE_SLAVE;
            hspi->Init.NSS = SPI_NSS_SOFT;
        }
    }
    else if (strcmp(cmd->args[1], "MASTER") == 0)
    {
        hspi->Init.Mode = SPI_MODE_MASTER;
        hspi->Init.NSS = SPI_NSS_SOFT;
    }
    else
    {
        Mimic_SendResponse("ERROR: Mode must be MASTER or SLAVE\r\n");
        return;
    }

    /* MASTER MODE CONFIG (Regular SPI) */
    hspi->Init.Direction = SPI_DIRECTION_2LINES;
    hspi->Init.DataSize = SPI_DATASIZE_8BIT;
    hspi->Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi->Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi->Init.FirstBit = SPI_FIRSTBIT_MSB;

    uint32_t pclk = (hspi->Instance == SPI1) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    uint32_t prescaler = SPI_BAUDRATEPRESCALER_256;
    if (pclk / 256 >= speed) prescaler = SPI_BAUDRATEPRESCALER_256;
    else if (pclk / 128 >= speed) prescaler = SPI_BAUDRATEPRESCALER_128;
    else if (pclk / 64 >= speed) prescaler = SPI_BAUDRATEPRESCALER_64;
    else if (pclk / 32 >= speed) prescaler = SPI_BAUDRATEPRESCALER_32;
    else if (pclk / 16 >= speed) prescaler = SPI_BAUDRATEPRESCALER_16;
    else if (pclk / 8 >= speed) prescaler = SPI_BAUDRATEPRESCALER_8;
    else if (pclk / 4 >= speed) prescaler = SPI_BAUDRATEPRESCALER_4;
    else prescaler = SPI_BAUDRATEPRESCALER_2;
    
    hspi->Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(hspi);
    Mimic_SendResponseF("OK: SPI %s Initialized\r\n", cmd->args[0]);
}

/**
  * @brief  SPI_SEND <INSTANCE> <DATA>
  */
void Mimic_CMD_SPI_SEND(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: SPI_SEND <1-5> <HEX_DATA>\r\n");
        Mimic_SendResponse("  Example: SPI_SEND 1 A5 3C FF\r\n");
        Mimic_SendResponse("  Example: SPI_SEND 2 0123456789ABCDEF\r\n");
        Mimic_SendResponse("  Note: CS is automatically controlled if configured in SPI_INIT\r\n");
        return;
    }
    
    SPI_HandleTypeDef *hspi = Mimic_GetSPIHandle(cmd->args[0]);
    if (hspi == NULL || hspi->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: SPI not initialized. Use SPI_INIT first.\r\n");
        return;
    }
    
    uint8_t spi_idx = atoi(cmd->args[0]) - 1;
    
    /* Combine all args as hex data */
    char hex_str[MIMIC_MAX_CMD_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++)
    {
        if (i > 1) strcat(hex_str, " ");
        strcat(hex_str, cmd->args[i]);
    }
    
    uint8_t data[64];
    uint16_t len = Mimic_ParseHexData(hex_str, data, sizeof(data));
    
    if (len == 0)
    {
        Mimic_SendResponse("ERROR: No valid hex data\r\n");
        return;
    }
    
    /* Assert CS if configured */
    if (spi_cs_pins[spi_idx].configured)
    {
        HAL_GPIO_WritePin(spi_cs_pins[spi_idx].port, spi_cs_pins[spi_idx].pin, GPIO_PIN_RESET);
    }
    
    HAL_StatusTypeDef status = HAL_SPI_Transmit(hspi, data, len, 1000);
    
    /* Deassert CS if configured */
    if (spi_cs_pins[spi_idx].configured)
    {
        HAL_GPIO_WritePin(spi_cs_pins[spi_idx].port, spi_cs_pins[spi_idx].pin, GPIO_PIN_SET);
    }
    
    if (status == HAL_OK)
    {
        Mimic_SendResponseF("OK: Sent %d bytes: ", len);
        for (int i = 0; i < len; i++)
        {
            Mimic_SendResponseF("%02X ", data[i]);
        }
        Mimic_SendResponse("\r\n");
    }
    else
    {
        Mimic_SendResponse("ERROR: Transmit failed\r\n");
    }
}

/**
  * @brief  SPI_RECV <INSTANCE> <LENGTH> [TIMEOUT_MS]
  */
void Mimic_CMD_SPI_RECV(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: SPI_RECV <1-5> <LENGTH> [TIMEOUT_MS]\r\n");
        Mimic_SendResponse("  Example: SPI_RECV 1 4 500\r\n");
        Mimic_SendResponse("  Note: CS is automatically controlled if configured in SPI_INIT\r\n");
        return;
    }
    
    SPI_HandleTypeDef *hspi = Mimic_GetSPIHandle(cmd->args[0]);
    if (hspi == NULL || hspi->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: SPI not initialized. Use SPI_INIT first.\r\n");
        return;
    }
    
    uint8_t spi_idx = atoi(cmd->args[0]) - 1;
    
    uint16_t length = atoi(cmd->args[1]);
    if (length > 64) length = 64;
    
    uint32_t timeout = 1000;
    if (cmd->argc >= 3)
    {
        timeout = atoi(cmd->args[2]);
    }
    
    uint8_t buffer[65] = {0};
    
    /* Assert CS if configured */
    if (spi_cs_pins[spi_idx].configured)
    {
        HAL_GPIO_WritePin(spi_cs_pins[spi_idx].port, spi_cs_pins[spi_idx].pin, GPIO_PIN_RESET);
    }
    
    HAL_StatusTypeDef status = HAL_SPI_Receive(hspi, buffer, length, timeout);
    
    /* Deassert CS if configured */
    if (spi_cs_pins[spi_idx].configured)
    {
        HAL_GPIO_WritePin(spi_cs_pins[spi_idx].port, spi_cs_pins[spi_idx].pin, GPIO_PIN_SET);
    }
    
    if (status == HAL_OK)
    {
        Mimic_SendResponseF("OK: Received %d bytes: ", length);
        for (int i = 0; i < length; i++)
        {
            Mimic_SendResponseF("%02X ", buffer[i]);
        }
        Mimic_SendResponse("\r\n");
    }
    else if (status == HAL_TIMEOUT)
    {
        Mimic_SendResponse("TIMEOUT: No data received\r\n");
    }
    else
    {
        Mimic_SendResponse("ERROR: Receive failed\r\n");
    }
}

/**
  * @brief  SPI_TRANSFER <INSTANCE> <DATA>
  */
void Mimic_CMD_SPI_TRANSFER(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: SPI_TRANSFER <1-5> <HEX_DATA>\r\n");
        Mimic_SendResponse("  Example: SPI_TRANSFER 1 A5 3C FF\r\n");
        Mimic_SendResponse("  Full-duplex: sends and receives simultaneously\r\n");
        Mimic_SendResponse("  Note: CS is automatically controlled if configured in SPI_INIT\r\n");
        return;
    }
    
    SPI_HandleTypeDef *hspi = Mimic_GetSPIHandle(cmd->args[0]);
    if (hspi == NULL || hspi->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: SPI not initialized. Use SPI_INIT first.\r\n");
        return;
    }
    
    uint8_t spi_idx = atoi(cmd->args[0]) - 1;
    
    /* Combine all args as hex data */
    char hex_str[MIMIC_MAX_CMD_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++)
    {
        if (i > 1) strcat(hex_str, " ");
        strcat(hex_str, cmd->args[i]);
    }
    
    uint8_t tx_data[64];
    uint8_t rx_data[64] = {0};
    uint16_t len = Mimic_ParseHexData(hex_str, tx_data, sizeof(tx_data));
    
    if (len == 0)
    {
        Mimic_SendResponse("ERROR: No valid hex data\r\n");
        return;
    }
    
    /* Assert CS if configured */
    if (spi_cs_pins[spi_idx].configured)
    {
        HAL_GPIO_WritePin(spi_cs_pins[spi_idx].port, spi_cs_pins[spi_idx].pin, GPIO_PIN_RESET);
    }
    
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(hspi, tx_data, rx_data, len, 1000);
    
    /* Deassert CS if configured */
    if (spi_cs_pins[spi_idx].configured)
    {
        HAL_GPIO_WritePin(spi_cs_pins[spi_idx].port, spi_cs_pins[spi_idx].pin, GPIO_PIN_SET);
    }
    
    if (status == HAL_OK)
    {
        Mimic_SendResponseF("OK: Transfer complete (%d bytes)\r\n", len);
        Mimic_SendResponse("  TX: ");
        for (int i = 0; i < len; i++)
        {
            Mimic_SendResponseF("%02X ", tx_data[i]);
        }
        Mimic_SendResponse("\r\n  RX: ");
        for (int i = 0; i < len; i++)
        {
            Mimic_SendResponseF("%02X ", rx_data[i]);
        }
        Mimic_SendResponse("\r\n");
    }
    else
    {
        Mimic_SendResponse("ERROR: Transfer failed\r\n");
    }
}

/**
  * @brief  SPI_CS <PIN> <HIGH|LOW>
  */
void Mimic_CMD_SPI_CS(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: SPI_CS <PIN> <HIGH|LOW>\r\n");
        Mimic_SendResponse("  Example: SPI_CS A4 LOW   (assert CS)\r\n");
        Mimic_SendResponse("  Example: SPI_CS A4 HIGH  (deassert CS)\r\n");
        return;
    }
    
    GPIO_TypeDef *port;
    uint16_t pin;
    
    if (!Mimic_ParsePin(cmd->args[0], &port, &pin))
    {
        Mimic_SendResponseF("ERROR: Invalid pin '%s'\r\n", cmd->args[0]);
        return;
    }
    
    /* Ensure pin is configured as output */
    if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
    
    if (strcmp(cmd->args[1], "HIGH") == 0)
    {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
        Mimic_SendResponseF("OK: CS %s = HIGH (deasserted)\r\n", cmd->args[0]);
    }
    else if (strcmp(cmd->args[1], "LOW") == 0)
    {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
        Mimic_SendResponseF("OK: CS %s = LOW (asserted)\r\n", cmd->args[0]);
    }
    else
    {
        Mimic_SendResponse("ERROR: State must be HIGH or LOW\r\n");
    }
}

/**
  * @brief  SPI_STATUS - Show SPI status
  */
void Mimic_CMD_SPI_STATUS(Mimic_Command_t *cmd)
{
    Mimic_SendResponse("SPI Status:\r\n");
    
    const char *spi_names[] = {"SPI1", "SPI2", "SPI3", "SPI4", "SPI5"};
    SPI_HandleTypeDef *spi_handles[] = {&hspi1, &hspi2, &hspi3, &hspi4, &hspi5};
    const char *spi_pins[][3] = {
        {"PA5/PA6/PA7", "PA4"},
        {"PB13/PB14/PB15", "PB12"},
        {"PB3/PB4/PB5", "PA15"},
        {"PE2/PE5/PE6", "PE4"},
        {"PE12/PE13/PE14", "PE11"}
    };
    
    for (int i = 0; i < 5; i++)
    {
        if (spi_handles[i]->Instance != NULL)
        {
            uint32_t pclk = (i == 0) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
            uint32_t prescaler = spi_handles[i]->Init.BaudRatePrescaler;
            uint32_t div = 2 << ((prescaler >> 3) & 0x07);
            uint32_t speed = pclk / div;
            
            Mimic_SendResponseF("  %s: %s, %lu Hz (%s/%s/%s)\r\n", 
                                spi_names[i],
                                spi_handles[i]->Init.Mode == SPI_MODE_MASTER ? "MASTER" : "SLAVE",
                                speed,
                                spi_pins[i][0],
                                spi_pins[i][1],
                                spi_handles[i]->Init.DataSize == SPI_DATASIZE_16BIT ? "16-bit" : "8-bit");
        }
        else
        {
            Mimic_SendResponseF("  %s: Not initialized\r\n", spi_names[i]);
        }
    }
}

/* ========================== I2C COMMANDS ================================== */

/**
  * @brief  Get I2C handle from string (1-3)
  */
I2C_HandleTypeDef* Mimic_GetI2CHandle(const char *instance)
{
    if (strcmp(instance, "1") == 0) return &hi2c1;
    if (strcmp(instance, "2") == 0) return &hi2c2;
    if (strcmp(instance, "3") == 0) return &hi2c3;
    return NULL;
}

/**
  * @brief  Configure I2C GPIO pins
  */
void Mimic_ConfigureI2CGPIO(I2C_TypeDef *instance)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    if (instance == I2C1)
    {
        /* I2C1: PB6 (SCL), PB7 (SDA) */
        __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        
        /* Disable Analog and Digital filters for maximum signal sensitivity */
        uint32_t *cr1 = (uint32_t*)((uint8_t*)I2C1 + 0x00);
        *cr1 &= ~(1 << 12); // Disable PEC (usually default)
        // Note: F4 series analog filter is in control registers if supported, 
        // older F401 might not have software-controllable analog filters 
        // depending on revision, but we reach for the manual bits.
    }
}

void Mimic_InitMPU6050Registers(void)
{
    memset(i2c_register_map, 0, 256);
    i2c_register_map[0x75] = 0x68; // WHO_AM_I
    i2c_register_map[0x6B] = 0x40; // Sleep mode enabled (Standard MPU6050 state)
    i2c_register_map[0x3B] = 0x00; // Accel X High
    i2c_register_map[0x3C] = 0x01; // Accel X Low
    i2c_register_map[0x3F] = 0x40; // Z-axis ~1g
}

/**
  * @brief  I2C_INIT <INSTANCE> <MASTER|SLAVE> <SPEED|OWN_ADDR> [ADDR_MODE]
  */
void Mimic_CMD_I2C_INIT(Mimic_Command_t *cmd)
{
    if (cmd->argc < 3)
    {
        Mimic_SendResponse("Usage: I2C_INIT <1-3> <MASTER|SLAVE> <SPEED|OWN_ADDR> [10]\r\n");
        Mimic_SendResponse("  Master: I2C_INIT 1 MASTER 100000    (100kHz)\r\n");
        Mimic_SendResponse("  Slave:  I2C_INIT 1 SLAVE 0x30       (Address 0x30)\r\n");
        return;
    }
    
    int idx = atoi(cmd->args[0]) - 1;
    if (idx < 0 || idx > 2)
    {
        Mimic_SendResponse("ERROR: Invalid I2C instance (use 1-3)\r\n");
        return;
    }
    
    I2C_HandleTypeDef *hi2c = Mimic_GetI2CHandle(cmd->args[0]);
    if (hi2c == NULL) return; // Should not happen with check above
    
    /* Parse Mode */
    uint8_t is_slave = 0;
    if (strcasecmp(cmd->args[1], "SLAVE") == 0) is_slave = 1;
    else if (strcasecmp(cmd->args[1], "MASTER") != 0)
    {
        Mimic_SendResponse("ERROR: Mode must be MASTER or SLAVE\r\n");
        return;
    }
    
    /* Parse Speed (Master) or Own Address (Slave) */
    uint32_t val = 0;
    if (is_slave) {
        val = (uint32_t)strtol(cmd->args[2], NULL, 16);
    } else {
        val = atoi(cmd->args[2]);
        if (val < 1000 || val > 400000)
        {
            Mimic_SendResponse("ERROR: Speed must be 1000-400000 Hz\r\n");
            return;
        }
    }
    
    /* Enable I2C clock and configure GPIO */
    if (hi2c == &hi2c1)
    {
        __HAL_RCC_I2C1_CLK_ENABLE();
        hi2c->Instance = I2C1;
        Mimic_ConfigureI2CGPIO(I2C1);
    }
    else if (hi2c == &hi2c2)
    {
        __HAL_RCC_I2C2_CLK_ENABLE();
        hi2c->Instance = I2C2;
        Mimic_ConfigureI2CGPIO(I2C2);
    }
    else if (hi2c == &hi2c3)
    {
        __HAL_RCC_I2C3_CLK_ENABLE();
        hi2c->Instance = I2C3;
        Mimic_ConfigureI2CGPIO(I2C3);
    }
    
    /* Common Config - Optimized for No-Resistor/Weak Pull-up */
    HAL_I2C_DeInit(hi2c);
    
    hi2c->Init.ClockSpeed = 100000;
    hi2c->Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c->Init.OwnAddress2 = 0;
    hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // Allow stretching (Master will wait if needed)
    
    /* Mode Specific Config */
    if (is_slave) {
        hi2c->Init.OwnAddress1 = (val << 1); 
        memset(i2c_register_map, 0, 256); // Clear map for new sensor
    } else {
        hi2c->Init.ClockSpeed = val;
        hi2c->Init.OwnAddress1 = 0;
    }
    
    if (HAL_I2C_Init(hi2c) != HAL_OK)
    {
        Mimic_SendResponse("ERROR: I2C init failed\r\n");
        return;
    }
    
    if (is_slave) {
        HAL_I2C_EnableListen_IT(hi2c);
        Mimic_SendResponseF("OK: I2C %d SLEEPING AS SLAVE 0x%02lX\r\n", idx+1, val);
    } else {
        Mimic_SendResponseF("OK: I2C %d MASTER AT %lu Hz\r\n", idx+1, val);
    }
    
    /* Disable Filters for better sensitivity with weak pull-ups */
    HAL_I2CEx_ConfigAnalogFilter(hi2c, I2C_ANALOGFILTER_DISABLE);
    HAL_I2CEx_ConfigDigitalFilter(hi2c, 0);
    
    /* Enable Interrupts for Slave Mode with Lower Priority than UART */
    if (is_slave) {
        HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0); 
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0); 
        HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
        
        // Start listening for master immediately
        HAL_I2C_EnableListen_IT(hi2c);
    }
    
    /* Save State */
    i2c_states[idx].is_slave = is_slave;
    
    Mimic_SendResponseF("OK: I2C%s initialized as %s\r\n", 
                        cmd->args[0], is_slave ? "SLAVE" : "MASTER");
    
    if (is_slave) {
        Mimic_SendResponseF("  Address: 0x%02X\r\n", val);
    } else {
        Mimic_SendResponseF("  Speed: %lu Hz\r\n", val);
    }
}

/**
  * @brief  I2C_SCAN <INSTANCE>
  */
void Mimic_CMD_I2C_SCAN(Mimic_Command_t *cmd)
{
    if (cmd->argc < 1)
    {
        Mimic_SendResponse("Usage: I2C_SCAN <1-3>\r\n");
        return;
    }
    
    I2C_HandleTypeDef *hi2c = Mimic_GetI2CHandle(cmd->args[0]);
    if (hi2c == NULL || hi2c->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: I2C not initialized. Use I2C_INIT first.\r\n");
        return;
    }
    
    Mimic_SendResponseF("Scanning I2C%s bus...\r\n", cmd->args[0]);
    
    uint8_t devices_found = 0;
    for (uint16_t addr = 1; addr < 128; addr++)
    {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr << 1), 2, 2) == HAL_OK)
        {
            Mimic_SendResponseF("Found device at 0x%02X\r\n", addr);
            devices_found++;
        }
    }
    
    if (devices_found == 0)
    {
        Mimic_SendResponse("No devices found\r\n");
    }
    else
    {
        Mimic_SendResponseF("Scan complete. Found %d devices.\r\n", devices_found);
    }
}

/**
  * @brief  I2C_WRITE <INSTANCE> <ADDR> <HEX_DATA>
  *         Master: Write to ADDR
  *         Slave:  ADDR is ignored, Send HEX_DATA to Master (when requested)
  */
void Mimic_CMD_I2C_WRITE(Mimic_Command_t *cmd)
{
    if (cmd->argc < 3)
    {
        Mimic_SendResponse("Usage: I2C_WRITE <1-3> <ADDR> <HEX_DATA>\r\n");
        Mimic_SendResponse("  Example: I2C_WRITE 1 0x68 6B 00\r\n");
        return;
    }
    
    int idx = atoi(cmd->args[0]) - 1;
    if (idx < 0 || idx > 2)
    {
        Mimic_SendResponse("ERROR: Invalid I2C instance\r\n");
        return;
    }
    
    I2C_HandleTypeDef *hi2c = Mimic_GetI2CHandle(cmd->args[0]);
    if (hi2c == NULL || hi2c->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: I2C not initialized\r\n");
        return;
    }
    
    uint16_t addr = (uint16_t)strtol(cmd->args[1], NULL, 16);
    
    /* Combine all args as hex data */
    char hex_str[MIMIC_MAX_CMD_LEN] = {0};
    for (int i = 2; i < cmd->argc; i++)
    {
        if (i > 2) strcat(hex_str, " ");
        strcat(hex_str, cmd->args[i]);
    }
    
    uint8_t data[64];
    uint16_t len = Mimic_ParseHexData(hex_str, data, sizeof(data));
    
    if (len == 0)
    {
        Mimic_SendResponse("ERROR: No valid hex data\r\n");
        return;
    }
    
    HAL_StatusTypeDef status;
    if (i2c_states[idx].is_slave)
    {
        Mimic_SendResponse("Wait for Master to read...\r\n");
        /* In Slave mode, we transmit when master requests */
        status = HAL_I2C_Slave_Transmit(hi2c, data, len, 5000);
    }
    else
    {
        status = HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr << 1), data, len, 1000);
    }
    
    if (status == HAL_OK)
    {
        Mimic_SendResponseF("OK: Write complete (%d bytes)\r\n", len);
    }
    else
    {
        Mimic_SendResponseF("ERROR: Write failed (Status: %d)\r\n", status);
    }
}

/**
  * @brief  I2C_READ <INSTANCE> <ADDR> <LENGTH>
  *         Master: Read from ADDR
  *         Slave:  ADDR is ignored, Receive LENGTH bytes from Master
  */
void Mimic_CMD_I2C_READ(Mimic_Command_t *cmd)
{
    if (cmd->argc < 3)
    {
        Mimic_SendResponse("Usage: I2C_READ <1-3> <ADDR> <LENGTH>\r\n");
        Mimic_SendResponse("  Example: I2C_READ 1 0x68 6\r\n");
        return;
    }
    
    int idx = atoi(cmd->args[0]) - 1;
    if (idx < 0 || idx > 2)
    {
        Mimic_SendResponse("ERROR: Invalid I2C instance\r\n");
        return;
    }
    
    I2C_HandleTypeDef *hi2c = Mimic_GetI2CHandle(cmd->args[0]);
    if (hi2c == NULL || hi2c->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: I2C not initialized\r\n");
        return;
    }
    
    uint16_t addr = (uint16_t)strtol(cmd->args[1], NULL, 16);
    uint16_t length = atoi(cmd->args[2]);
    if (length > 64) length = 64;
    
    uint8_t buffer[65] = {0};
    HAL_StatusTypeDef status;
    
    if (i2c_states[idx].is_slave)
    {
        /* In Slave mode, we listen for Master events */
        if (HAL_I2C_EnableListen_IT(hi2c) != HAL_OK)
        {
            Mimic_SendResponse("ERROR: I2C listen failed\r\n");
            return;
        }
        Mimic_SendResponse("OK: Slave listening on bus...\r\n");
    }
    else
    {
        status = HAL_I2C_Master_Receive(hi2c, (uint16_t)(addr << 1), buffer, length, 1000);
        if (status == HAL_OK)
        {
            Mimic_SendResponseF("OK: Read %d bytes: ", length);
            for (int i = 0; i < length; i++)
            {
                Mimic_SendResponseF("%02X ", buffer[i]);
            }
            Mimic_SendResponse("\r\n");
        }
        else
        {
            Mimic_SendResponseF("ERROR: Read failed (Status: %d)\r\n", status);
        }
    }
}

/**
  * @brief  I2C_SLAVE_DATA <INSTANCE> <HEX>
  *         Pre-load the buffer that will be sent when a Master Reads from us.
  */
void Mimic_CMD_I2C_REG_SET(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: I2C_REG_SET <ADDR> <VAL>\r\n");
        return;
    }
    
    uint8_t addr = (uint8_t)strtoul(cmd->args[0], NULL, 0);
    uint8_t val = (uint8_t)strtoul(cmd->args[1], NULL, 0);
    
    i2c_register_map[addr] = val;
    Mimic_SendResponseF("OK: Reg[0x%02X] = 0x%02X\r\n", addr, val);
}

void Mimic_CMD_I2C_REG_DATA(Mimic_Command_t *cmd)
{
    if (cmd->argc < 2)
    {
        Mimic_SendResponse("Usage: I2C_REG_DATA <START_ADDR> <HEX>\r\n");
        return;
    }
    
    uint8_t addr = (uint8_t)strtoul(cmd->args[0], NULL, 0);
    uint16_t len = Mimic_ParseHexData(cmd->args[1], &i2c_register_map[addr], 256 - addr);
    
    Mimic_SendResponseF("OK: Loaded %d bytes starting at 0x%02X\r\n", len, addr);
}

/**
  * @brief  I2C_WRITE_READ <INSTANCE> <ADDR> <WRITE_DATA> <READ_LEN>
  */
void Mimic_CMD_I2C_WRITE_READ(Mimic_Command_t *cmd)
{
    if (cmd->argc < 4)
    {
        Mimic_SendResponse("Usage: I2C_WRITE_READ <1-3> <ADDR> <WRITE_DATA> <READ_LEN>\r\n");
        Mimic_SendResponse("  Example: I2C_WRITE_READ 1 0x68 75 1  (Read WHO_AM_I)\r\n");
        return;
    }
    
    I2C_HandleTypeDef *hi2c = Mimic_GetI2CHandle(cmd->args[0]);
    if (hi2c == NULL || hi2c->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: I2C not initialized\r\n");
        return;
    }
    
    uint16_t addr = (uint16_t)strtol(cmd->args[1], NULL, 16);
    
    /* Parse write data */
    uint8_t write_data[64];
    uint16_t write_len = Mimic_ParseHexData(cmd->args[2], write_data, sizeof(write_data));
    
    uint16_t read_len = atoi(cmd->args[3]);
    if (read_len > 64) read_len = 64;
    
    /* Write first (without stop if possible, but HAL uses stop) 
       HAL_I2C_Mem_Read is better for register access */
    
    HAL_StatusTypeDef status;
    uint8_t buffer[65] = {0};
    
    /* If write length is 1 or 2 bytes, we can use Mem_Read which does repeated start */
    if (write_len == 1)
    {
        status = HAL_I2C_Mem_Read(hi2c, (uint16_t)(addr << 1), write_data[0], I2C_MEMADD_SIZE_8BIT, buffer, read_len, 1000);
    }
    else if (write_len == 2)
    {
        uint16_t mem_addr = (write_data[0] << 8) | write_data[1];
        status = HAL_I2C_Mem_Read(hi2c, (uint16_t)(addr << 1), mem_addr, I2C_MEMADD_SIZE_16BIT, buffer, read_len, 1000);
    }
    else
    {
        /* Fallback for longer writes: Write then Read (two transactions) */
        status = HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr << 1), write_data, write_len, 1000);
        if (status == HAL_OK)
        {
            status = HAL_I2C_Master_Receive(hi2c, (uint16_t)(addr << 1), buffer, read_len, 1000);
        }
    }
    
    if (status == HAL_OK)
    {
        Mimic_SendResponseF("OK: Read %d bytes from 0x%02X: ", read_len, addr);
        for (int i = 0; i < read_len; i++)
        {
            Mimic_SendResponseF("%02X ", buffer[i]);
        }
        Mimic_SendResponse("\r\n");
    }
    else
    {
        Mimic_SendResponseF("ERROR: Write/Read failed (Status: %d)\r\n", status);
    }
}

void Mimic_CMD_I2C_STATUS(Mimic_Command_t *cmd)
{
    Mimic_SendResponse("I2C Status:\r\n");
    
    const char *i2c_names[] = {"I2C1", "I2C2", "I2C3"};
    I2C_HandleTypeDef *i2c_handles[] = {&hi2c1, &hi2c2, &hi2c3};
    const char *i2c_pins[][2] = {
        {"PB6(SCL)", "PB7(SDA)"},
        {"PB10(SCL)", "PB11(SDA)"},
        {"PA8(SCL)", "PC9(SDA)"}
    };
    
    for (int i = 0; i < 3; i++)
    {
        if (i2c_handles[i]->Instance != NULL)
        {
            uint8_t is_slave = i2c_states[i].is_slave;
            
            Mimic_SendResponseF("  %s: Initialized (%s)\r\n", 
                                i2c_names[i], is_slave ? "SLAVE" : "MASTER");
            
            if (is_slave) {
                Mimic_SendResponseF("       Addr: 0x%02X, Mode: %s\r\n", 
                                    i2c_handles[i]->Init.OwnAddress1 >> 1,
                                    i2c_handles[i]->Init.AddressingMode == I2C_ADDRESSINGMODE_10BIT ? "10-bit" : "7-bit");
            } else {
                Mimic_SendResponseF("       Speed: %lu Hz, Mode: %s\r\n", 
                                    i2c_handles[i]->Init.ClockSpeed,
                                    i2c_handles[i]->Init.AddressingMode == I2C_ADDRESSINGMODE_10BIT ? "10-bit" : "7-bit");
            }
            
            Mimic_SendResponseF("       Pins: %s, %s\r\n", i2c_pins[i][0], i2c_pins[i][1]);
        }
        else
        {
            Mimic_SendResponseF("  %s: Not initialized\r\n", i2c_names[i]);
        }
    }
}

/* ========================== SYSTEM COMMANDS =============================== */

/**
  * @brief  HELP - Show available commands
  */
void Mimic_CMD_HELP(Mimic_Command_t *cmd)
{
    Mimic_SendResponse("\r\n");
    Mimic_SendResponse("=== MIMIC Command Reference ===\r\n\r\n");
    
    Mimic_SendResponse("[GPIO Commands]\r\n");
    Mimic_SendResponse("  PIN_STATUS <PIN>         - Show pin config (e.g., PIN_STATUS D12)\r\n");
    Mimic_SendResponse("  PIN_SET_OUT <PIN>        - Set as output\r\n");
    Mimic_SendResponse("  PIN_SET_IN <PIN> [PULL]  - Set as input (PULL: UP|DOWN|NONE)\r\n");
    Mimic_SendResponse("  PIN_HIGH <PIN>           - Set pin HIGH\r\n");
    Mimic_SendResponse("  PIN_LOW <PIN>            - Set pin LOW\r\n");
    Mimic_SendResponse("  PIN_READ <PIN>           - Read pin state\r\n");
    Mimic_SendResponse("  PIN_TOGGLE <PIN>         - Toggle pin\r\n");
    Mimic_SendResponse("  PIN_MODE <PIN> <MODE>    - Set mode (IN|OUT|AF|AN)\r\n\r\n");
    
    Mimic_SendResponse("[UART Commands]\r\n");
    Mimic_SendResponse("  UART_INIT <1|6> <BAUD> [PARITY] [STOP]\r\n");
    Mimic_SendResponse("    Example: UART_INIT 1 9600 N 1\r\n");
    Mimic_SendResponse("  UART_SEND <1|6> <DATA>   - Send data\r\n");
    Mimic_SendResponse("  UART_RECV <1|6> <LEN> [TIMEOUT]\r\n");
    Mimic_SendResponse("  UART_STATUS              - Show UART status\r\n\r\n");
    
    Mimic_SendResponse("[SPI Commands]\r\n");
    Mimic_SendResponse("  SPI_INIT <1-5> <MASTER|SLAVE> <SPEED> [CPOL] [CPHA] [SIZE] [ORDER]\r\n");
    Mimic_SendResponse("    Example: SPI_INIT 1 MASTER 1000000\r\n");
    Mimic_SendResponse("    Example: SPI_INIT 2 MASTER 500000 1 1 8 MSB\r\n");
    Mimic_SendResponse("  SPI_SEND <1-5> <HEX>     - Send data (half-duplex)\r\n");
    Mimic_SendResponse("  SPI_RECV <1-5> <LEN> [TIMEOUT]\r\n");
    Mimic_SendResponse("  SPI_TRANSFER <1-5> <HEX> - Full-duplex transfer\r\n");
    Mimic_SendResponse("  SPI_CS <PIN> <HIGH|LOW>  - Control chip select\r\n");
    Mimic_SendResponse("  SPI_STATUS               - Show SPI status\r\n\r\n");
    
    Mimic_SendResponse("[I2C Commands]\r\n");
    Mimic_SendResponse("  I2C_INIT <1-3> <MASTER|SLAVE> <SPEED|ADDR> [MODE_10] - Init I2C\r\n");
    Mimic_SendResponse("  I2C_SCAN <1-3>                      - Scan bus\r\n");
    Mimic_SendResponse("  I2C_WRITE <1-3> <ADDR> <HEX>        - Write data\r\n");
    Mimic_SendResponse("  I2C_READ <1-3> <ADDR> <LEN>         - Read data\r\n");
    Mimic_SendResponse("  I2C_WRITE_READ <1-3> <ADDR> <HEX> <LEN> - Write then read\r\n");
    Mimic_SendResponse("  I2C_STATUS                          - Show I2C status\r\n\r\n");
    
    Mimic_SendResponse("[System Commands]\r\n");
    Mimic_SendResponse("  HELP                     - Show this help\r\n");
    Mimic_SendResponse("  VERSION                  - Show version\r\n");
    Mimic_SendResponse("  STATUS                   - System status\r\n");
    Mimic_SendResponse("  RESET                    - Reset MCU\r\n\r\n");
    
    Mimic_SendResponse("[Short Commands]\r\n");
    Mimic_SendResponse("  PS=PIN_STATUS, PSO=PIN_SET_OUT, PSI=PIN_SET_IN\r\n");
    Mimic_SendResponse("  PH=PIN_HIGH, PL=PIN_LOW, PR=PIN_READ, PT=PIN_TOGGLE\r\n");
    Mimic_SendResponse("  UI=UART_INIT, US=UART_SEND, UR=UART_RECV\r\n");
    Mimic_SendResponse("  SI=SPI_INIT, SS=SPI_SEND, SR=SPI_RECV, ST=SPI_TRANSFER\r\n");
    Mimic_SendResponse("  II=I2C_INIT, IS=I2C_SCAN, IW=I2C_WRITE, IR=I2C_READ, IWR=WRITE_READ\r\n\r\n");
}

/**
  * @brief  VERSION - Show firmware version
  */
void Mimic_CMD_VERSION(Mimic_Command_t *cmd)
{
    Mimic_SendResponseF("MIMIC Firmware v%s\r\n", MIMIC_VERSION);
    Mimic_SendResponse("Target: STM32F411CEU6 BlackPill\r\n");
    Mimic_SendResponseF("Built: %s %s\r\n", __DATE__, __TIME__);
}

/**
  * @brief  STATUS - Show system status
  */
void Mimic_CMD_STATUS(Mimic_Command_t *cmd)
{
    Mimic_SendResponse("\r\n=== System Status ===\r\n");
    Mimic_SendResponseF("Uptime: %lu ms\r\n", HAL_GetTick());
    Mimic_SendResponseF("Host UART: %lu baud\r\n", MIMIC_HOST_UART.Init.BaudRate);
    
    /* GPIO clocks */
    Mimic_SendResponse("\r\nGPIO Clocks Enabled:\r\n");
    if (RCC->AHB1ENR & RCC_AHB1ENR_GPIOAEN) Mimic_SendResponse("  GPIOA ");
    if (RCC->AHB1ENR & RCC_AHB1ENR_GPIOBEN) Mimic_SendResponse("  GPIOB ");
    if (RCC->AHB1ENR & RCC_AHB1ENR_GPIOCEN) Mimic_SendResponse("  GPIOC ");
    if (RCC->AHB1ENR & RCC_AHB1ENR_GPIODEN) Mimic_SendResponse("  GPIOD ");
    if (RCC->AHB1ENR & RCC_AHB1ENR_GPIOEEN) Mimic_SendResponse("  GPIOE ");
    Mimic_SendResponse("\r\n");
}

/**
  * @brief  RESET - Software reset
  */
void Mimic_CMD_RESET(Mimic_Command_t *cmd)
{
    Mimic_SendResponse("Resetting...\r\n");
    HAL_Delay(100);
    NVIC_SystemReset();
}

/**
  * @brief  UART_POLL <INSTANCE> <MESSAGE> <COUNT> [DELAY_MS]
  *         Continuously send a message
  */
void Mimic_CMD_UART_POLL(Mimic_Command_t *cmd)
{
    if (cmd->argc < 3)
    {
        Mimic_SendResponse("Usage: UART_POLL <1|6> <MESSAGE> <COUNT> [DELAY_MS]\r\n");
        Mimic_SendResponse("  Example: UART_POLL 1 Hello 10 500\r\n");
        Mimic_SendResponse("  Sends 'Hello' 10 times with 500ms delay\r\n");
        Mimic_SendResponse("  Use COUNT=0 for infinite (until reset)\r\n");
        return;
    }
    
    UART_HandleTypeDef *huart = Mimic_GetUARTHandle(cmd->args[0]);
    if (huart == NULL)
    {
        Mimic_SendResponse("ERROR: Invalid UART instance\r\n");
        return;
    }
    
    if (huart->Instance == NULL)
    {
        Mimic_SendResponse("ERROR: UART not initialized. Use UART_INIT first.\r\n");
        return;
    }
    
    const char *message = cmd->args[1];
    uint32_t count = atoi(cmd->args[2]);
    uint32_t delay_ms = 500;  // default 500ms
    
    if (cmd->argc >= 4)
    {
        delay_ms = atoi(cmd->args[3]);
    }
    
    uint8_t len = strlen(message);
    uint8_t tx_buf[66];
    
    // Add newline to message
    snprintf((char*)tx_buf, sizeof(tx_buf), "%s\r\n", message);
    len = strlen((char*)tx_buf);
    
    Mimic_SendResponseF("Sending '%s' %lu times (delay: %lu ms)\r\n", 
                        message, count == 0 ? 0xFFFFFFFF : count, delay_ms);
    Mimic_SendResponse("(Reset MCU to stop if infinite)\r\n");
    
    uint32_t sent = 0;
    uint32_t infinite = (count == 0);
    
    while (infinite || sent < count)
    {
        HAL_UART_Transmit(huart, tx_buf, len, 100);
        sent++;
        
        if (!infinite && sent >= count)
            break;
            
        HAL_Delay(delay_ms);
    }
    
    Mimic_SendResponseF("OK: Sent %lu messages\r\n", sent);
}
/* ========================== I2C CALLBACKS ================================ */

static uint8_t i2c_first_byte_received = 0;

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode)
{
    if (TransferDirection == I2C_DIRECTION_TRANSMIT)
    {
        /* Master is writing. Prepare to receive the register pointer. */
        i2c_first_byte_received = 0;
        HAL_I2C_Slave_Sequential_Receive_IT(hi2c, i2c_slave_rx_buffer, 1, I2C_FIRST_FRAME);
    }
    else
    {
        /* Master is reading. Send from the register map. */
        uint16_t len = 256 - current_reg_addr;
        HAL_I2C_Slave_Sequential_Transmit_IT(hi2c, &i2c_register_map[current_reg_addr], len, I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (i2c_first_byte_received == 0)
    {
        /* Capture register address and move to data reception */
        current_reg_addr = i2c_slave_rx_buffer[0];
        i2c_first_byte_received = 1;
        
        // Prepare to receive data if the master continues to write
        HAL_I2C_Slave_Sequential_Receive_IT(hi2c, &i2c_register_map[current_reg_addr], 1, I2C_NEXT_FRAME);
    }
    else
    {
        /* Continuing data write: update map and increment pointer */
        current_reg_addr++;
        HAL_I2C_Slave_Sequential_Receive_IT(hi2c, &i2c_register_map[current_reg_addr], 1, I2C_NEXT_FRAME);
    }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    /* Transmission complete. I2C peripheral is automatically ready for next AddrCallback. */
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_I2C_EnableListen_IT(hi2c);
}

/* ========================== SPI CALLBACKS ================================ */

// ACCURATE BMP280 SPI SLAVE CALLBACK
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        uint8_t rx_byte = spi_rx_buf[0];
        
        if (spi_state == 0) // Address Byte received
        {
            // BMP280 SPI: Bit 7 is 1 for Read, 0 for Write. 
            // Register addresses are 7-bit (0x00-0x7F).
            current_reg_addr = rx_byte & 0x7F; 
            spi_state = 1;
            
            // Prepare the FIRST data byte for the NEXT transfer
            // We use the 7-bit address for our internal map
            spi_tx_buf[0] = i2c_register_map[current_reg_addr];
        }
        else // Data Byte (Master is reading or writing)
        {
            // Auto-increment for burst reads
            current_reg_addr = (current_reg_addr + 1) & 0x7F;
            spi_tx_buf[0] = i2c_register_map[current_reg_addr];
            
            // HEARTBEAT: Slightly increment temperature for simulation
            if (current_reg_addr == 0x7A) { // 0xFA & 0x7F = 0x7A
                i2c_register_map[0x7A]++;
            }
        }
        
        HAL_SPI_TransmitReceive_IT(hspi, (uint8_t*)&spi_tx_buf[0], (uint8_t*)&spi_rx_buf[0], 1);
    }
}

// Reset SPI on CS Falling Edge (via EXTI)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_4) // CS (PA4)
    {
        if (!(GPIOA->IDR & GPIO_PIN_4)) // Falling edge (CS Active)
        {
            spi_state = 0;
            spi_tx_buf[0] = 0x00; 
            HAL_SPI_Abort(&hspi1);
            HAL_SPI_TransmitReceive_IT(&hspi1, (uint8_t*)&spi_tx_buf[0], (uint8_t*)&spi_rx_buf[0], 1);
        }
    }
}

// Check CS (PA4) in the main loop to reset sync
void Mimic_SPI_Sync(void)
{
    if (GPIOA->IDR & GPIO_PIN_4) // CS is HIGH (Idle)
    {
        spi_state = 0;
        // Pre-load the ID register into tx_buf for the next burst
        spi_tx_buf[0] = i2c_register_map[0xD0];
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        // Reset SPI Slave state on error
        HAL_SPI_Abort_IT(hspi);
        HAL_SPI_TransmitReceive_IT(hspi, spi_tx_buf, spi_rx_buf, 1);
    }
}

/* ========================== END OF FILE =================================== */
