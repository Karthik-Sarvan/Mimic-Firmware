import re

def process_file(filepath, replacements):
    with open(filepath, 'r') as f:
        content = f.read()
    
    for old, new in replacements.items():
        content = re.sub(old, new, content, flags=re.MULTILINE | re.DOTALL)
        
    with open(filepath, 'w') as f:
        f.write(content)

# 1. Update mimic.h
h_replacements = {
    r'extern UART_HandleTypeDef huart6;\n': '',
    r'extern SPI_HandleTypeDef hspi2;\nextern SPI_HandleTypeDef hspi3;\nextern SPI_HandleTypeDef hspi4;\nextern SPI_HandleTypeDef hspi5;\n': '',
    r'extern I2C_HandleTypeDef hi2c2;\nextern I2C_HandleTypeDef hi2c3;\n': ''
}
process_file('Mimic/Core/Inc/mimic.h', h_replacements)

# 2. Update mimic.c
c_replacements = {
    # Remove port mapping for D, E, H
    r"\{'D', GPIOD\},\s*\{'E', GPIOE\},\s*\{'H', GPIOH\},": "{'H', GPIOH},",
    # Remove extra UART handles
    r"UART_HandleTypeDef huart6;": "",
    # Remove extra SPI handles
    r"SPI_HandleTypeDef hspi2;\nSPI_HandleTypeDef hspi3;\nSPI_HandleTypeDef hspi4;\nSPI_HandleTypeDef hspi5;\n": "",
    # Remove extra I2C handles
    r"I2C_HandleTypeDef hi2c2;\nI2C_HandleTypeDef hi2c3;\n": "",
    r"static Mimic_SPI_CS_t spi_cs_pins\[5\]": "static Mimic_SPI_CS_t spi_cs_pins[1]",
    r"static Mimic_I2C_State_t i2c_states\[3\]": "static Mimic_I2C_State_t i2c_states[1]",
    # UART
    r'(static UART_HandleTypeDef\* Mimic_GetUARTHandle\(const char \*instance\)\s*\{[^\}]+?)else if\s*\(strcmp\(instance,\s*"UART6".*?\}\s*return NULL;': r'\1return NULL;',
    r'(static void Mimic_ConfigureUARTGPIO\(USART_TypeDef \*instance\)\s*\{[^\}]+?HAL_GPIO_Init\(GPIOA,\s*&GPIO_InitStruct\);\s*\})[\s\n]*else if\s*\(instance\s*==\s*USART6\)[\s\S]*?HAL_GPIO_Init\(GPIOC,\s*&GPIO_InitStruct\);\s*\}': r'\1',
    r'(if\s*\(huart\s*==\s*&huart1\)\s*\{[^}]+\})[\s\n]*else if\s*\(huart\s*==\s*&huart6\)\s*\{[^}]+\}': r'\1',
    r'(if\s*\(huart1\.Instance\s*!=\s*NULL\)\s*\{.*?\}[\s\n]*else\s*\{.*?\}).*?if\s*\(huart6\.Instance\s*!=\s*NULL\)\s*\{.*?\}[\s\n]*else\s*\{.*?\}': r'\1',
    # SPI
    r'(if\s*\(instance\s*==\s*SPI1\)\s*\{.*?HAL_GPIO_Init\(GPIOA,\s*&GPIO_InitStruct\);\s*\})[\s\n]*else if\s*\(instance\s*==\s*SPI2\).*?HAL_GPIO_Init\(GPIOE,\s*&GPIO_InitStruct\);\s*\}': r'\1',
    # I2C
    r'(if\s*\(instance\s*==\s*I2C1\)\s*\{.*?HAL_GPIO_Init\(GPIOB,\s*&GPIO_InitStruct\);\s*\})[\s\n]*else if\s*\(instance\s*==\s*I2C2\).*?HAL_GPIO_Init\(GPIOC,\s*&GPIO_InitStruct\);\s*\}': r'\1',
}
process_file('Mimic/Core/Src/mimic.c', c_replacements)
print("Files rewritten successfully.")
