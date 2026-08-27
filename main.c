/*******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the CAN 2.0 PSoC4  Application
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/
/*******************************************************************************
 * Header file includes
 ******************************************************************************/
#include "cy_pdl.h"
#include "cy_syslib.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include <inttypes.h>
#include <stdio.h>
#include <cy_utils.h>

/*******************************************************************************
 * Macros
 ******************************************************************************/
/* Define the Node Number. This value must be unique within the network and 
 * should be less than or equal to MAX_NODE_IN_NETWORK 
 */
#define CAN_NODE                            (0x01u)

/* Max number of Node in Network*/
#define MAX_NODE_IN_NETWORK                 (2u)

/* CAN Standard identifier */
#define CY_CAN_STD_MSG_ID0                  (0x00u)

/* CAN Extended Message identifier */
#define CY_CAN_EXTD_MSG_ID0                 (0x10000000u)

/* User button debounce to send Standard ID frame */
#define DEBOUNCE_SEND_STD_FRAME             (10u)

/* User button debounce to send extended ID frame */
#define DEBOUNCE_SEND_EXTD_FRAME            (2000u)

/* CAN Global interrupt number */
#define CY_CANFD_INTERRUPT                  (can_interrupt_can_IRQn)

/* SCB BLOCK3-UART TX port */
#define CYBSP_DEBUG_UART_TX                 (P7_1)

/* SCB BLOCK3-UART RX port */
#define CYBSP_DEBUG_UART_RX                 (P7_0)
/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/
/* CAN interrupt handler */
void CanInterruptHandler(void);

/* Button press interrupt handler */
static void isr_button (void);

/* CAN reception callback */
void CAN_RxMsgCallback(uint8_t index,
                       cy_stc_can_message_frame_t* rxMsg);
/*******************************************************************************
 * Constant Variables
 ******************************************************************************/
/* CAN interrupt configuration structure */
const cy_stc_sysint_t can_irq_cfg =
{
    /* Source of interrupt signal */
    .intrSrc = CY_CANFD_INTERRUPT,
    /* Interrupt priority */
    .intrPriority = 0U,
};

/* User button (P3.7) interrupt configuration structure */
cy_stc_sysint_t switch_intr_config =
{
    /* Source of interrupt signal */
    .intrSrc = CYBSP_USER_BTN_IRQ,
    /* Interrupt priority */
    .intrPriority = 2U,
};

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
/* Prepares a CAN message to transmit frame with standard ID*/
cy_stc_can_message_frame_t txStdIdFrmDataMsg =
{
    .id = CY_CAN_STD_MSG_ID0 + CAN_NODE,
    .data = {CAN_NODE, 0x00000000u },
    .length = 8u,
    .rtr = false,
    .ide = false
};

/* Prepares a CAN message to transmit frame with Extended ID*/
cy_stc_can_message_frame_t txExtdIdFrmDataMsg =
{
    .id = CY_CAN_EXTD_MSG_ID0 + CAN_NODE,
    .data = {CAN_NODE, 0x00000000u },
    .length =  8u,
    .rtr = false,
    .ide = true
};
  
/* Allocate context for UART operation */
cy_stc_scb_uart_context_t CYBSP_UART_context;

/* This is a shared context structure */
cy_stc_can_context_t CY_CAN_context;

/* Flag to send standard ID frame */
static volatile bool sendStdFrame = false;

/* Flag to send Extended ID frame */
static volatile bool sendExtdFrame = false;

/* RX buffer to store the received frame */
cy_stc_can_message_frame_t RxbufferDataMsg;

/* RX frame with standard ID count */
uint8_t rx_std_frm_count;

/* RX frame with Extended ID count */
uint8_t rx_extd_frm_count;

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  Configures the CAN interface, associated interrupts, a user push-button, 
*  and an LED indicator. In the infinite loop, the code debounces the button 
*  and transmits either a standard or extended CAN frame depending on which 
*  debounce constant is met (DEBOUNCE_SEND_STD_FRAME or 
*  DEBOUNCE_SEND_EXTD_FRAME).
*
*  When a received CAN frame’s ID falls within the range
*  CY_CAN_STD_MSG_ID0…CY_CAN_STD_MSG_ID0 + MAX_NODE_IN_NETWORK
*  (for standard IDs) or CY_CAN_EXTD_MSG_ID0…CY_CAN_EXTD_MSG_ID0 + 
*  MAX_NODE_IN_NETWORK(for extended IDs), the frame is “loop-backed” by adding  
*  MAX_NODE_IN_NETWORK to its ID and retransmitting it.
*
*  After loop-back:
*    - USER_LED1 toggles on receipt of standard-ID frames.
*    - USER_LED10 toggles on receipt of extended-ID frames.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/

int main(void)
{

    cy_rslt_t result;
    cy_en_can_status_t status;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io for uart logging */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, 
                                CY_RETARGET_IO_BAUDRATE);
                                
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }   

    printf("==========================================================\r\n");
    printf("Welcome to CAN example\r\n");
    printf("==========================================================\r\n\n");

    printf("==========================================================\r\n");
    printf("CAN Node-%d\r\n", CAN_NODE);
    printf("==========================================================\r\n");

    /* Hook the interrupt service routine and enable the interrupt for CAN*/
    (void) Cy_SysInt_Init(&can_irq_cfg, CanInterruptHandler);
    NVIC_EnableIRQ(can_irq_cfg.intrSrc);


    /* Hook the interrupt service routine and enable the interrupt for
     * user button */
    (void)Cy_SysInt_Init(&switch_intr_config, isr_button);
    NVIC_EnableIRQ(CYBSP_USER_BTN_IRQ);

    /* Initialize the CAN Peripheral */
    status =  Cy_CAN_Init (CY_CAN_HW, &CY_CAN_config, &CY_CAN_context);

    if (status != CY_CAN_SUCCESS)
    {
        CY_ASSERT(0);
    }

    for (;;)
    {
        /* Send frame with SID if USER Button is presses for 
         * DEBOUNCE_SEND_STD_FRAME
         */
        if (true == sendStdFrame)
        {
            /* Reset the count for RX frame with standard ID */
            rx_std_frm_count = 0u;          
            printf("\r\nSend data frame with Standard ID 0x%08" PRIx32 , 
                    txStdIdFrmDataMsg.id);   
            printf("\r\nTx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x" 
                   " 0x%02x 0x%02x\r\n", 
                   CY_HI8(CY_HI16(txStdIdFrmDataMsg.data[0])),
                   CY_LO8(CY_HI16(txStdIdFrmDataMsg.data[0])), 
                   CY_HI8(CY_LO16(txStdIdFrmDataMsg.data[0])),
                   CY_LO8(CY_LO16(txStdIdFrmDataMsg.data[0])), 
                   CY_HI8(CY_HI16(txStdIdFrmDataMsg.data[1])), 
                   CY_LO8(CY_HI16(txStdIdFrmDataMsg.data[1])), 
                   CY_HI8(CY_LO16(txStdIdFrmDataMsg.data[1])), 
                   CY_LO8(CY_LO16(txStdIdFrmDataMsg.data[1])));                          
            /* Sends the prepared data frame for STD ID using tx buffer 0 */
            status = Cy_CAN_Transmit(CY_CAN_HW, 0u, &txStdIdFrmDataMsg, true,
                                     false, &CY_CAN_context);     
            if (CY_CAN_SUCCESS != status)
            {
                /* Error processing */
                CY_ASSERT(0);
            }
            sendStdFrame = false;
        }
        /* Send frame   EXID if USER Button is presses for 
         * DEBOUNCE_SEND_EXTD_FRAME
         */
        else if(true == sendExtdFrame)
        {
            /* Reset the count for RX frame with extended ID */
            rx_extd_frm_count = 0u;
            printf("\r\nSend data frame with Extended ID 0x%08" PRIx32, 
                    txExtdIdFrmDataMsg.id);
            printf("\r\nTx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
                   " 0x%02x 0x%02x\r\n", 
                   CY_HI8(CY_HI16(txExtdIdFrmDataMsg.data[0])),
                   CY_LO8(CY_HI16(txExtdIdFrmDataMsg.data[0])), 
                   CY_HI8(CY_LO16(txExtdIdFrmDataMsg.data[0])),
                   CY_LO8(CY_LO16(txExtdIdFrmDataMsg.data[0])), 
                   CY_HI8(CY_HI16(txExtdIdFrmDataMsg.data[1])), 
                   CY_LO8(CY_HI16(txExtdIdFrmDataMsg.data[1])), 
                   CY_HI8(CY_LO16(txExtdIdFrmDataMsg.data[1])), 
                   CY_LO8(CY_LO16(txExtdIdFrmDataMsg.data[1])));
            /* Sends the prepared data frame for EXTD ID using tx buffer 0 */
            status = Cy_CAN_Transmit(CY_CAN_HW, 1u, &txExtdIdFrmDataMsg, true, 
                                     false, &CY_CAN_context);
            if (CY_CAN_SUCCESS != status)
            {
                /* Error processing */
                CY_ASSERT(0);
            }
            sendExtdFrame = false;
        }
    }
}

/*******************************************************************************
* Function Name: isr_button
********************************************************************************
* Summary:
* This is the callback function for button press
*
* Parameters:
*    None
*
*******************************************************************************/
static void isr_button (void)
{
    uint32_t debounce = 0u;
    
    /* Clears the triggered pin interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);

    NVIC_ClearPendingIRQ(CYBSP_USER_BTN_IRQ);
    
    /* calculate button press time in ms */
    while(0u == (Cy_GPIO_Read(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN)))
    {
        debounce++;
        Cy_SysLib_Delay(1u);
    }
    
    if((debounce < DEBOUNCE_SEND_EXTD_FRAME) && 
       (debounce > DEBOUNCE_SEND_STD_FRAME))
    {
        /* Set flag to send standard ID frame */
        sendStdFrame = 1u;
    }
    else if(debounce > DEBOUNCE_SEND_EXTD_FRAME)
    {
        /* Set flag to send Extended ID frame */
        sendExtdFrame = 1u;
    }

}

/*******************************************************************************
* Function Name: CanInterruptHandler
********************************************************************************
* Summary:
* This is the interrupt handler function for the can interrupt.
*
* Parameters:
*  none
*
*
*******************************************************************************/
void CanInterruptHandler(void)
{
    /* Just call the IRQ handler with the context */
    Cy_CAN_IrqHandler(CY_CAN_HW, &CY_CAN_context);
}

/*******************************************************************************
* Function Name: CAN_RxMsgCallback
********************************************************************************
* Summary:
* This is the callback function for CAN reception
*
* Parameters:
*   index                     RX_buffer number of the received message
*   rxMsg                     Message buffer.
*******************************************************************************/
void CAN_RxMsgCallback(uint8_t index,
                       cy_stc_can_message_frame_t* rxMsg)
{
    cy_en_can_status_t status;
    
    /* Check received data frame's ID is STD id */
    if(rxMsg->id <= (CY_CAN_STD_MSG_ID0 + MAX_NODE_IN_NETWORK))
    {
        /* Show received frame data on terminal */
        printf("\r\n%u bytes received from Node-%u with identifier 0x%08"  
              PRIx32, rxMsg->length, 
                      (unsigned int)(rxMsg->data[0]), 
                      rxMsg->id );         
        printf("\r\nRx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
               " 0x%02x\r\n", CY_HI8(CY_HI16(rxMsg -> data[0])),
                                  CY_LO8(CY_HI16(rxMsg -> data[0])), 
                                  CY_HI8(CY_LO16(rxMsg -> data[0])),
                                  CY_LO8(CY_LO16(rxMsg -> data[0])), 
                                  CY_HI8(CY_HI16(rxMsg -> data[1])), 
                                  CY_LO8(CY_HI16(rxMsg -> data[1])), 
                                  CY_HI8(CY_LO16(rxMsg -> data[1])), 
                                  CY_LO8(CY_LO16(rxMsg -> data[1])));    
        
        /* loop-back the received data by updating ID*/
        RxbufferDataMsg.id = rxMsg->id + MAX_NODE_IN_NETWORK; 
        RxbufferDataMsg.ide = rxMsg->ide;
        RxbufferDataMsg.data[0] = CAN_NODE;
        RxbufferDataMsg.data[1] = rxMsg -> data[1];
        RxbufferDataMsg.length = rxMsg -> length;
        
        printf("\r\nSending(looping back) data frame with Standard ID 0x%08" 
                PRIx32, RxbufferDataMsg.id);
        printf("\r\nTx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
               " 0x%02x\r\n", 
               CY_HI8(CY_HI16(RxbufferDataMsg.data[0])),
               CY_LO8(CY_HI16(RxbufferDataMsg.data[0])), 
               CY_HI8(CY_LO16(RxbufferDataMsg.data[0])),
               CY_LO8(CY_LO16(RxbufferDataMsg.data[0])), 
               CY_HI8(CY_HI16(RxbufferDataMsg.data[1])), 
               CY_LO8(CY_HI16(RxbufferDataMsg.data[1])), 
               CY_HI8(CY_LO16(RxbufferDataMsg.data[1])), 
               CY_LO8(CY_LO16(RxbufferDataMsg.data[1])));               
                
        /* Sends the prepared data frame using tx buffer 0 */
        status = Cy_CAN_Transmit(CY_CAN_HW, 0u, &RxbufferDataMsg, true, false,
                                 &CY_CAN_context);
        
        if (CY_CAN_SUCCESS != status)
        {
            /* Error processing */
            CY_ASSERT(0);
        }
    }
    /* Check received data frame's ID is EXTD id */
    else if((rxMsg->id >= CY_CAN_EXTD_MSG_ID0) && 
            (rxMsg->id <= CY_CAN_EXTD_MSG_ID0 + MAX_NODE_IN_NETWORK))
    {
        /* Show received frame data on terminal */
        printf("\r\n%u bytes received from Node-%u with identifier 0x%08" 
               PRIx32 , rxMsg->length, 
                       (unsigned int)(rxMsg->data[0]), 
                       rxMsg->id ); 
        
        printf("\r\nRx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
               " 0x%02x\r\n", CY_HI8(CY_HI16(rxMsg -> data[0])),
                                  CY_LO8(CY_HI16(rxMsg -> data[0])), 
                                  CY_HI8(CY_LO16(rxMsg -> data[0])),
                                  CY_LO8(CY_LO16(rxMsg -> data[0])), 
                                  CY_HI8(CY_HI16(rxMsg -> data[1])), 
                                  CY_LO8(CY_HI16(rxMsg -> data[1])), 
                                  CY_HI8(CY_LO16(rxMsg -> data[1])), 
                                  CY_LO8(CY_LO16(rxMsg -> data[1])));    
                
        /* loop-back the received data by updating ID*/
        RxbufferDataMsg.id = rxMsg->id + MAX_NODE_IN_NETWORK; 
        RxbufferDataMsg.ide = rxMsg -> ide;
        RxbufferDataMsg.data[0] = CAN_NODE;
        RxbufferDataMsg.data[1] = rxMsg -> data[1];
        RxbufferDataMsg.length = rxMsg -> length;
        
        printf("\r\nSending data frame with Extended ID %08" PRIx32, 
                RxbufferDataMsg.id);                
        printf("\r\nTx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
               " 0x%02x\r\n", 
               CY_HI8(CY_HI16(RxbufferDataMsg.data[0])),
               CY_LO8(CY_HI16(RxbufferDataMsg.data[0])), 
               CY_HI8(CY_LO16(RxbufferDataMsg.data[0])),
               CY_LO8(CY_LO16(RxbufferDataMsg.data[0])), 
               CY_HI8(CY_HI16(RxbufferDataMsg.data[1])), 
               CY_LO8(CY_HI16(RxbufferDataMsg.data[1])), 
               CY_HI8(CY_LO16(RxbufferDataMsg.data[1])), 
               CY_LO8(CY_LO16(RxbufferDataMsg.data[1])));    
                                  
        /* Sends the prepared data frame using tx buffer 0 */
        status = Cy_CAN_Transmit(CY_CAN_HW, 0u, &RxbufferDataMsg, true, false,
                                 &CY_CAN_context);
        
        if (CY_CAN_SUCCESS != status)
        {
            /* Error processing */
            CY_ASSERT(0);
        }   
    }
    else if(rxMsg->id == CY_CAN_STD_MSG_ID0 + MAX_NODE_IN_NETWORK + CAN_NODE)
    {
        /* Incrementing received standard ID frame count*/
        rx_std_frm_count++;
        
        /* Show received frame data on terminal */
        printf("\r\n%u bytes received from Node- %u with identifier 0x%08"  
               PRIx32, rxMsg->length, 
                       (unsigned int)(rxMsg->data[0]), 
                       rxMsg->id );      
        printf("\r\nRx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
               " 0x%02x", CY_HI8(CY_HI16(rxMsg -> data[0])),
                          CY_LO8(CY_HI16(rxMsg -> data[0])), 
                          CY_HI8(CY_LO16(rxMsg -> data[0])),
                          CY_LO8(CY_LO16(rxMsg -> data[0])), 
                          CY_HI8(CY_HI16(rxMsg -> data[1])), 
                          CY_LO8(CY_HI16(rxMsg -> data[1])), 
                          CY_HI8(CY_LO16(rxMsg -> data[1])), 
                          CY_LO8(CY_LO16(rxMsg -> data[1])));
        
        if(rx_std_frm_count == (MAX_NODE_IN_NETWORK - 1u))  
        {
            printf(" --> Toggling an USER LED 1 \r\n");
            /* Inverting User LED status */
            Cy_GPIO_Inv(CYBSP_LED1_PORT, CYBSP_LED1_PIN);
            /* Resetting received standard ID frame count */
            rx_std_frm_count = 0U;
        }
    }
    else if(rxMsg->id == CY_CAN_EXTD_MSG_ID0 + MAX_NODE_IN_NETWORK + CAN_NODE) 
    {
        /* Incrementing received extended ID frame count */
        rx_extd_frm_count++;
        
        /* Show received frame data on terminal */
        printf("\r\n%u bytes received from Node-%u with identifier 0x%08"  
               PRIx32, rxMsg->length, 
                       (unsigned int)(rxMsg->data[0]), 
                       rxMsg->id ); 
        printf("\r\nRx Data : 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x"
               " 0x%02x", CY_HI8(CY_HI16(rxMsg -> data[0])),
                          CY_LO8(CY_HI16(rxMsg -> data[0])), 
                          CY_HI8(CY_LO16(rxMsg -> data[0])),
                          CY_LO8(CY_LO16(rxMsg -> data[0])), 
                          CY_HI8(CY_HI16(rxMsg -> data[1])), 
                          CY_LO8(CY_HI16(rxMsg -> data[1])), 
                          CY_HI8(CY_LO16(rxMsg -> data[1])), 
                          CY_LO8(CY_LO16(rxMsg -> data[1])));
        
        if(rx_extd_frm_count == (MAX_NODE_IN_NETWORK - 1u)) 
        {
            printf(" --> Toggling an USER LED 10 \r\n");
            /* Inverting User LED status */
            Cy_GPIO_Inv(CYBSP_LED10_PORT, CYBSP_LED10_PIN);
            /* Resetting received extended ID frame count */
            rx_extd_frm_count = 0U;
        }
   }
    CY_UNUSED_PARAMETER(index);
}

/* [] END OF FILE */
