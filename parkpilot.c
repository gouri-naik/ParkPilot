#include <stdio.h>
#include <LPC17xx.h>
#include <string.h>

#define LED_Pinsel 0xFF // P0.4-0.11 (LEDs)
#define TRIGGER_PIN (1 << 15) // P0.15 (Trigger Pin)
#define ECHO_PIN (1 << 16) // P0.16 (Echo Pin)
#define BUZZER_PIN (1 << 17) // P0.17 (Buzzer Pin)
#define SEVEN_SEG_PORT 0x0F // P1.23-P1.26 (7-segment display)

// 7-segment display patterns (common cathode)
unsigned char seg_code[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

char ans[20] = "";
char status[20] = "";
int temp, temp1, temp2 = 0;
int flag = 0, flag_command = 0;
int i, j, k, l, r, echoTime = 5000;
float distance = 0;
float prev_distance = 0;
int parking_counter = 0;
int stable_count = 0;
int is_parked = 0;
int bill_amount = 0;
int show_bill = 0;
int bill_display_counter = 0;

void lcd_wr(void);
void port_wr(void);
void delay(int r1);
void timer_start(void);
float timer_stop();
void timer_init(void);
void delay_in_US(unsigned int microseconds);
void delay_in_MS(unsigned int milliseconds);
void display_7seg(int digit);

void delay_in_US(unsigned int microseconds)
{
    LPC_TIM0->TCR = 0x02;
    LPC_TIM0->PR = 0; // Set prescaler to the value of 0
    LPC_TIM0->MR0 = microseconds - 1; // Set match register
    LPC_TIM0->MCR = 0x01; // Interrupt on match
    LPC_TIM0->TCR = 0x01; // Enable timer
    while ((LPC_TIM0->IR & 0x01) == 0); // Wait for interrupt flag
    LPC_TIM0->TCR = 0x00; // Stop the timer
    LPC_TIM0->IR = 0x01; // Clear the interrupt flag
}

void delay_in_MS(unsigned int milliseconds)
{
    delay_in_US(milliseconds * 1000);
}

void timer_init(void)
{
    LPC_TIM0->CTCR = 0x0;
    LPC_TIM0->PR = 11999999; // To maintain 12Mhz as per specified for LPC 1768
    LPC_TIM0->TCR = 0x02; // Reset Timer
}

void timer_start(void)
{
    LPC_TIM0->TCR = 0x02; // Reset Timer
    LPC_TIM0->TCR = 0x01; // Enable timer
}

float timer_stop()
{
    LPC_TIM0->TCR = 0x0;
    return LPC_TIM0->TC;
}

void delay(int r1)
{
    for (r = 0; r < r1; r++);
}

void port_wr()
{
    int j;
    LPC_GPIO0->FIOPIN = temp2 << 23;
    if (flag_command == 0) {
        LPC_GPIO0->FIOCLR = 1 << 27;
    }
    else {
        LPC_GPIO0->FIOSET = 1 << 27;
    }
    LPC_GPIO0->FIOSET = 1 << 28;
    for (j = 0; j < 50; j++);
    LPC_GPIO0->FIOCLR = 1 << 28;
    for (j = 0; j < 10000; j++);
}

void lcd_wr()
{
    temp2 = (temp1 >> 4) & 0xF;
    port_wr();
    temp2 = temp1 & 0xF;
    port_wr();
}

void display_7seg(int digit)
{
		unsigned char code;
    if (digit > 9) digit = 9; // Limit to single digit
    code = seg_code[digit];
    
    // Write to P1.23-P1.26 (4 bits for 7-segment)
    LPC_GPIO1->FIOCLR = SEVEN_SEG_PORT << 23;
    LPC_GPIO1->FIOSET = ((code & 0x0F) << 23);
}

int main()
{
    int command_init[] = {3, 3, 3, 2, 2, 0x01, 0x06, 0x0C, 0x80};
    int loop_count = 0;
    
    SystemInit();
    SystemCoreClockUpdate();
    timer_init();
    
    LPC_PINCON->PINSEL0 &= 0xFFFFF00F; // LEDs P0.4-P0.11
    LPC_PINCON->PINSEL0 &= 0x3FFFFFFF; // TRIG P0.15
    LPC_PINCON->PINSEL1 &= 0xfffffff0; // ECHO P0.16
    LPC_PINCON->PINSEL3 &= 0xF00FFFFF; // 7-segment P1.23-P1.26
    
    LPC_GPIO0->FIODIR |= TRIGGER_PIN | BUZZER_PIN; // Direction for TRIGGER and BUZZER
    LPC_GPIO1->FIODIR |= 0 << 16; // Direction for ECHO PIN
    LPC_GPIO0->FIODIR |= LED_Pinsel << 4; // Direction for LED
    LPC_GPIO1->FIODIR |= SEVEN_SEG_PORT << 23; // Direction for 7-segment
    LPC_PINCON->PINSEL1 |= 0;
    LPC_GPIO0->FIODIR |= 0XF << 23 | 1 << 27 | 1 << 28; // Direction For LCDs
    
    flag_command = 0;
    for (i = 0; i < 9; i++)
    {
        temp1 = command_init[i];
        lcd_wr();
        for (j = 0; j < 30000; j++);
    }
    
    i = 0;
    flag = 1;
    LPC_GPIO0->FIOCLR |= TRIGGER_PIN;
    
    // Initialize 7-segment to 0
    display_7seg(0);
    
    while (1) {
				float distance_diff;
        // Trigger ultrasonic sensor
        LPC_GPIO0->FIOSET = 0x00000800;
        LPC_GPIO0->FIOMASK = 0xFFFF7FFF;
        LPC_GPIO0->FIOPIN |= TRIGGER_PIN;
        delay_in_US(10);
        LPC_GPIO0->FIOCLR |= TRIGGER_PIN;
        LPC_GPIO0->FIOMASK = 0x0;
        
        // Wait for echo
        while (!(LPC_GPIO0->FIOPIN & ECHO_PIN));
        timer_start();
        while (LPC_GPIO0->FIOPIN & ECHO_PIN);
        echoTime = timer_stop();
        
        // Calculate distance
        distance = (0.00343 * echoTime) / 2;
        
        // Check if distance is stable (car parked)
        distance_diff = distance - prev_distance;
        if (distance_diff < 0) distance_diff = -distance_diff; // Absolute value
        
        if (distance_diff < 2.0 && distance < 50) { // Stable within 2cm and car present
            stable_count++;
            if (stable_count >= 10 && !is_parked) { // 10 seconds stable
                is_parked = 1;
                parking_counter = 0;
                show_bill = 0;
            }
        }
        else {
            stable_count = 0;
            if (is_parked) {
                // Car moved - calculate bill
                is_parked = 0;
                bill_amount = parking_counter / 10;
                show_bill = 1;
                bill_display_counter = 0; // Start 15 second timer
                display_7seg(bill_amount % 10);
            }
        }
        
        prev_distance = distance;
        
        // Update bill display timer
        if (show_bill) {
            bill_display_counter++;
            if (bill_display_counter >= 15) { // 15 seconds elapsed
                show_bill = 0;
                bill_display_counter = 0;
            }
        }
        
        // Update parking counter if parked
        if (is_parked) {
            loop_count++;
            if (loop_count >= 1) { // Increment every second
                parking_counter++;
                loop_count = 0;
                display_7seg(parking_counter % 10); // Show last digit
            }
        }
        else if (show_bill) {
            // Keep showing bill
            display_7seg(bill_amount % 10);
        }
        else {
            // Not parked - show 0
            display_7seg(0);
        }
        
        // Determine status based on distance
        if (distance < 10) {
            sprintf(status, "STOP");
        }
        else if (distance >= 10 && distance < 20) {
            sprintf(status, "WARNING");
        }
        else {
            sprintf(status, "SAFE");
        }
        
        // Prepare display string with distance and parking info
        if (is_parked) {
            sprintf(ans, "PARKED T:%ds", parking_counter);
        }
        else if (show_bill) {
            sprintf(ans, "Bill: Rs.%d", bill_amount);
        }
        else {
            sprintf(ans, "Dist:%.1fcm", distance);
        }
        
        // Clear LCD and display on line 1
        flag_command = 0;
        temp1 = 0x01; // Clear display
        lcd_wr();
        for (j = 0; j < 30000; j++);
        
        temp1 = 0x80; // Move cursor to line 1
        lcd_wr();
        
        flag_command = 1;
        i = 0;
        while (ans[i] != '\0') {
            temp1 = ans[i];
            lcd_wr();
            for (j = 0; j < 30000; j++);
            i++;
        }
        
        // Display status on line 2
        flag_command = 0;
        temp1 = 0xC0; // Move cursor to line 2
        lcd_wr();
        
        flag_command = 1;
        i = 0;
        while (status[i] != '\0') {
            temp1 = status[i];
            lcd_wr();
            for (j = 0; j < 30000; j++);
            i++;
        }
        
        // Control LEDs and Buzzer based on distance
        if (distance < 10) {
            // STOP: All LEDs ON, Buzzer ON
            LPC_GPIO0->FIOSET = LED_Pinsel << 4;
            LPC_GPIO0->FIOSET = BUZZER_PIN;
        }
        else if (distance >= 10 && distance < 20) {
            // WARNING: Half LEDs ON, Buzzer ON
            LPC_GPIO0->FIOSET = 0x0F << 4; // P0.4-P0.7 ON
            LPC_GPIO0->FIOCLR = 0xF0 << 4; // P0.8-P0.11 OFF
            LPC_GPIO0->FIOSET = BUZZER_PIN;
        }
        else {
            // SAFE: All LEDs OFF, Buzzer OFF
            LPC_GPIO0->FIOCLR = LED_Pinsel << 4;
            LPC_GPIO0->FIOCLR = BUZZER_PIN;
        }
        
        delay(88000); // ~1 second delay
    }
}
