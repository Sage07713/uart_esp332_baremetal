#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/uart_struct.h"
#include "soc/uart_reg.h"
#include "soc/gpio_sig_map.h"
#include "esp_rom_gpio.h"

#define GPIO_OUT_REG        (*((volatile uint32_t *)0x3FF44004))
#define GPIO_ENABLE_REG     (*((volatile uint32_t *)0x3FF44020))
#define LED_PIN     2
#define LED_BIT     (1 << LED_PIN)

#define BAUD_CLK_DIVIDER    694   // 115200 baud

#define DPORT_PERIP_CLK_EN_REG   (*((volatile uint32_t *)0x3FF000C0))
#define DPORT_PERIP_RST_EN_REG   (*((volatile uint32_t *)0x3FF000C4))
#define DPORT_UART1_CLK_EN       (1 << 5)
#define DPORT_UART1_RST          (1 << 5)

char rx_buffer[128];
int rx_index = 0;

void uart1_enable_clock(void) {
    DPORT_PERIP_CLK_EN_REG |= DPORT_UART1_CLK_EN;
    DPORT_PERIP_RST_EN_REG &= ~DPORT_UART1_RST;
}

void uart1_init(void) {
    UART1.clk_div.div_int = BAUD_CLK_DIVIDER;
    UART1.clk_div.div_frag = 0;
    UART1.conf0.bit_num = 3;
    UART1.conf0.parity_en = 0;
    UART1.conf0.stop_bit_num = 1;
    UART1.conf0.txd_inv = 0;
    UART1.conf0.rxfifo_rst = 1;
    UART1.conf0.rxfifo_rst = 0;
    UART1.conf0.txfifo_rst = 1;
    UART1.conf0.txfifo_rst = 0;
}

void uart1_putc(char c) {
    while (UART1.status.txfifo_cnt >= 128) ;
    UART1.fifo.rw_byte = c;
}

void uart1_puts(const char *s) {
    while (*s) uart1_putc(*s++);
}

int uart1_try_getc(char *out) {
    if (UART1.status.rxfifo_cnt > 0) {
        *out = UART1.fifo.rw_byte;
        return 1;
    }
    return 0;
}

void uart0_putc(char c) {
    while (UART0.status.txfifo_cnt >= 128) ;
    UART0.fifo.rw_byte = c;
}
void uart0_puts(const char *s) {
    while (*s) uart0_putc(*s++);
}
void uart0_init(void) {
    UART0.clk_div.div_int = BAUD_CLK_DIVIDER;
    UART0.clk_div.div_frag = 0;
    UART0.conf0.bit_num = 3;
    UART0.conf0.parity_en = 0;
    UART0.conf0.stop_bit_num = 1;
}

void app_main(void) {
    GPIO_ENABLE_REG |= LED_BIT;

    uart0_init();

    uart1_enable_clock();   // <-- critical: enable UART1 before touching its registers
    uart1_init();

    esp_rom_gpio_connect_out_signal(17, U1TXD_OUT_IDX, false, false);
    esp_rom_gpio_connect_in_signal(16, U1RXD_IN_IDX, false);
    GPIO_ENABLE_REG |= (1 << 17);

    uart0_puts("UART1 loopback test starting\r\n");
    uart0_puts("Make sure GPIO17 is jumpered to GPIO16\r\n");

    int counter = 0;
    while (1) {
        GPIO_OUT_REG |= LED_BIT;
        vTaskDelay(200 / portTICK_PERIOD_MS);
        GPIO_OUT_REG &= ~LED_BIT;
        vTaskDelay(200 / portTICK_PERIOD_MS);

        counter++;
        if (counter % 5 == 0) {
            uart0_puts("Sending 'hello' on UART1...\r\n");
            uart1_puts("hello\r\n");
        }

        char c;
        while (uart1_try_getc(&c)) {
            if (c == '\r' || c == '\n') {
                if (rx_index > 0) {
                    rx_buffer[rx_index] = '\0';
                    uart0_puts("UART1 received: ");
                    uart0_puts(rx_buffer);
                    uart0_puts("\r\n");
                    rx_index = 0;
                }
            } else if (rx_index < (int)sizeof(rx_buffer) - 1) {
                rx_buffer[rx_index++] = c;
            }
        }
    }
}