#include "mimic.h"

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

