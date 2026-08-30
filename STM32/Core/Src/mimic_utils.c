#include "mimic.h"

static const Mimic_PortMap_t port_map[] = {
    {'A', GPIOA},
    {'B', GPIOB},
    {'C', GPIOC},
    {'H', GPIOH},
    {0, NULL}
};

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


uint16_t Mimic_ParseHexData(const char *hex_str, uint8_t *data, uint16_t max_len)
{
    uint16_t count = 0;
    const char *ptr = hex_str;
    
    while (*ptr && count < max_len)
    {
        // Skip whitespace
        while (*ptr == ' ' || *ptr == '	') ptr++;
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

