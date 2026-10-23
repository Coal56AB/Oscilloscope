#include "console.h"
#include "xparameters.h"
#include "xuartps_hw.h"

#if XPAR_XUARTPS_0_UART_CLK_FREQ_HZ != 100000000 || XPAR_XUARTPS_0_BASEADDR != STDOUT_BASEADDRESS
#error Recalculate console divisors for the configured PS UART
#endif

void board_console_init(void)
{
    u32 mode = XUartPs_ReadReg(STDOUT_BASEADDRESS, XUARTPS_MR_OFFSET);
    /* Vivado 2019.1 ps7_init retains 115200 divisors for a 1000000 request. */
    /* Its standalone driver also limits SetBaudRate to 921600. */
    XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_CR_OFFSET,
                    XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS);
    XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_MR_OFFSET,
                    mode & ~XUARTPS_MR_CLKSEL);
    /* 100 MHz / (20 * (4 + 1)) = 1 Mbaud. Keep the PS7 8N1 format. */
    XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_BAUDGEN_OFFSET, 20);
    XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_BAUDDIV_OFFSET, 4);
    XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_CR_OFFSET,
                    XUARTPS_CR_TXRST | XUARTPS_CR_RXRST);
    XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_CR_OFFSET,
                    XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN);
}
