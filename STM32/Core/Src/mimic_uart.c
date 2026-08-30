#include "mimic.h"

typedef struct {
    GPIO_TypeDef *de_port;
    uint16_t de_pin;
    uint8_t de_active_high;
    uint8_t configured;
} Mimic_UART_RS485_t;

static Mimic_UART_RS485_t uart_rs485_config[3] = {0}; // Index: 0=USART1, 1=USART2, 2=USART6

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

