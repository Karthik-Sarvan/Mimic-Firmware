#include "mimic.h"

I2C_HandleTypeDef hi2c1;
void Mimic_SPI_Sync(void);
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;

/* I2C State tracking */
static Mimic_I2C_State_t i2c_states[3] = {0}; // 0=I2C1, 1=I2C2, 2=I2C3
static uint8_t i2c_slave_rx_buffer[64];

/* Shadow Register Map for fast sensor emulation */
uint8_t i2c_register_map[256] = {0};
uint8_t current_reg_addr = 0;

/* SPI Slave Buffers */
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

