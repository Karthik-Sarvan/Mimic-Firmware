/* ============================================================================
 * UART USAGE EXAMPLE - Add to main.c
 * ============================================================================
 * 
 * This shows how to use the generated UART driver in your main loop.
 * Copy the relevant parts into your main.c file.
 * ============================================================================
 */

/* 1. Include the header at the top of main.c */
#include "usart.h"

/* 2. In main(), after MX_xxx_Init() calls, initialize UART */
// MX_USART2_UART_Init();  // Already called if using CubeMX

/* 3. In the main while(1) loop, add: */

while (1)
{
    /* Process incoming UART data (non-blocking) */
    UART_Process();
    
    /* Check if a complete frame was received */
    if (UART_IsFrameReady())
    {
        uint16_t len;
        uint8_t *data = UART_GetData(&len);
        
        /* Example: Echo back with prefix */
        UART_TransmitString("Received: ");
        UART_Transmit(data, len);
        UART_TransmitString("\r\n");
        
        /* Example: Command parsing */
        if (strcmp((char*)data, "LED ON") == 0)
        {
            // HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            UART_TransmitString("LED is ON\r\n");
        }
        else if (strcmp((char*)data, "LED OFF") == 0)
        {
            // HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            UART_TransmitString("LED is OFF\r\n");
        }
        else if (strcmp((char*)data, "STATUS") == 0)
        {
            UART_TransmitString("System OK\r\n");
        }
        
        /* Clear buffer for next frame */
        UART_ClearBuffer();
    }
    
    /* Your other application code here */
}

/* ============================================================================
 * END OF EXAMPLE
 * ============================================================================ */
