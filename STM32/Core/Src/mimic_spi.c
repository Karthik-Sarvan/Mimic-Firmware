#include "mimic.h"

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi3;
SPI_HandleTypeDef hspi4;
SPI_HandleTypeDef hspi5;

/* SPI CS pin tracking */
static Mimic_SPI_CS_t spi_cs_pins[5] = {0};  // One for each SPI instance

static uint8_t spi_tx_buf[2] = {0};
static uint8_t spi_rx_buf[2] = {0};
volatile uint8_t spi_state = 0;
volatile uint8_t spi_active = 0;

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

    /* Parse CPOL (0: LOW, 1: HIGH, default 0) */
    if (cmd->argc >= 4 && strcmp(cmd->args[3], "1") == 0)
    {
        hspi->Init.CLKPolarity = SPI_POLARITY_HIGH;
    }
    else
    {
        hspi->Init.CLKPolarity = SPI_POLARITY_LOW;
    }

    /* Parse CPHA (0: 1EDGE, 1: 2EDGE, default 0) */
    if (cmd->argc >= 5 && strcmp(cmd->args[4], "1") == 0)
    {
        hspi->Init.CLKPhase = SPI_PHASE_2EDGE;
    }
    else
    {
        hspi->Init.CLKPhase = SPI_PHASE_1EDGE;
    }

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

