#include "mimic.h"

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

