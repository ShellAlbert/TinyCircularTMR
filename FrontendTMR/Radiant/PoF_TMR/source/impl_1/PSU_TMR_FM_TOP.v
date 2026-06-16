module PSU_TMR_FM_TOP(

	input iClk_12Mhz,
	
    //Dynamic Power Management Auxiliary. 
    output oIAM_ALIVE,
    //Laser Diode Power Enabled.
    output oLD_PWR_EN,
 
    //PSU UART.
    output oPSU_UART_TxD,
    input iPSU_UART_RxD,

    //AD7988 SPI-Compatible Interface.
	output oSPI_SDI,
	output oSPI_CNV,
	output oSPI_SCK,
	input iSPI_SDO, 

    //Debug LED*3.
	output reg oLED0,
    output reg oLED1,
    output oLED2_oRX_PWR_EN /*replicated with oRX_PWR_EN*/
)/* synthesis RGB_TO_GPIO = "oLED1, oLED2, oLED3" */;

//HSOSC
//High-frequency oscillator.
//Generates 48-MHz nominal clock, +/- 10 percent, with user-programmable divider. 
//Can drive global clock network or fabric routing.
//Input Ports
//CLKHFPU :Power up the oscillator. After power up, output will be stable after 100 ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¯ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¿ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â½s. Active high.
//CLKHFEN :Enable the clock output. Enable should be low for the 100-ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¯ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¿ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â½s power-up period. Active high.
//Output Ports
//CLKHF :Oscillator output
//Parameters
//CLKHF_DIV
//Clock divider selection:
//0'b00 = 48 MHz
//0'b01 = 24 MHz
//0'b10 = 12 MHz
//0'b11 = 6 MHz
wire clk_48MHz;
//By default, the outputs are routed to global clock network. 
//To route to local fabric, see the examples in the Appendix: Design Entry section.
HSOSC #(.CLKHF_DIV("0b00")) //48 MHz
my_HSOSC(
    .CLKHFPU(1'b1), 
    .CLKHFEN(1'b1), 
    .CLKHF(clk_48MHz)
)/* synthesis ROUTE_THROUGH_FABRIC= 0 */; //the value can be either 0 or 1

///////////////////////////////////////////////////////////////////
//We are not allowed to use PLL output, because it's exclusive with Pin-35. ADQ[7].
//if PLL use internal clock source, Pin-35 can be used as output.
//PLL: 48MHz->66MHz.
//WARNING!!!!!
//If I configured PLL outputs 70MHz, it doesn't work correctly.
//Then I slowed down to 66MHz, it starts to work.
//66MHz is not reliable, down to 48MHz.

//ERROR <67201318> - 
//When PLL.OUTCORE or PLL.OUTGLOBAL is used, the input IO at site 'PR13B' can only drive PLL.REFERENCECLK due to architecture constraint. 
//When PLL is utilized in the design, the I/O site 'PR13B' can only be used exclusively as a PLL clock input. 
//If PLL uses an internal clock, the I/O site 'PR13B' can be used as an output.
wire rst_n;
wire clk_48MHz_Global;
wire clk_48MHz_Fabric;
ZPLL ic_pll(
	//.ref_clk_i(clk_48MHz), 
	.ref_clk_i(iClk_12Mhz), 
	.rst_n_i(1'b1), 
	.lock_o(rst_n), 
	.outcore_o(clk_48MHz_Fabric), 
	.outglobal_o(clk_48MHz_Global)
);

//Output IAMALIVE=1 to auxiliary Power Management.
assign oIAM_ALIVE=1;

//In TMR Project, ADC needs to run continuously.
//Turn on Laser Diode always.
assign oLD_PWR_EN=1;

//No Photodiode Receive required at this project.
//So turn off load switch to save power consumption.
assign oLED2_oRX_PWR_EN=0;

//LED1 Flashs at 1Hz frequency.
//48MHz/1Hz/2=24_000_000.
reg [31:0] cnt_1;
always @(posedge clk_48MHz_Global or negedge rst_n)
if(!rst_n) begin
    oLED0<=0; 
    cnt_1<=0;
end
else begin
    if(cnt_1==32'd24_000_000) begin
        cnt_1<=0;
        oLED0<=~oLED0;
    end
    else begin
        cnt_1<=cnt_1+1;
    end
end
//////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////
reg adc_en;
wire adc_valid;
wire [15:0] adc_data;
ZAD7988_Controller u1_ad7988(
	//common signals.
	.iClk(clk_48MHz_Global), //input clock.
	.iRstN(rst_n),
	.iEn(adc_en),

	//AD7988 SPI-Compatible Interface.
	.oSDI(oSPI_SDI),
	.oCNV(oSPI_CNV),
	.oSCK(oSPI_SCK),
	.iSDO(iSPI_SDO), 

	//acquisition data output interface.
	.oData(adc_data),
	.oDataValid(adc_valid)
);
/////////////////////////////////////////////////////////////////////////
//Tx Random UART Data at 4Mbps.
//generate 4MHz Clock, //48MHz/4MHz=12.
reg [15:0] tx_data_16bits;
reg [7:0] tx_data;
reg tx_en;
wire tx_done;
ZUART_Tx #(.Freq_divider(12)) uart_u1
(
	.iClk(clk_48MHz_Global),
	.iRst_N(rst_n),
	.iData(tx_data),
	
	//pull down iEn to start transmition until pulse done oDone was issued.
	.iEn(tx_en),
	.oDone(tx_done),
	.oTxD(oPSU_UART_TxD)
);

//AD7988-1. Maximum Throughput Rate:100KHz.
//48MHz/100KHz=480.
reg [15:0] cnt_100KHz;
always @(posedge clk_48MHz_Global or negedge rst_n) 
if(!rst_n) begin
    cnt_100KHz<=0;
end
else begin
    if(cnt_100KHz==480-1) begin cnt_100KHz<=0; end
    else begin cnt_100KHz<=cnt_100KHz+1; end
end
wire tick_100KHz;
assign tick_100KHz=(cnt_100KHz==480-1)?1:0;

//driven by step_i.
reg [7:0] step_i, step_i_return; // all variables are 8bits.
reg [31:0] cnt_delay;
reg [15:0] frame_head={16'h55AA};
reg [7:0] cnt_led1;
always @(posedge clk_48MHz_Global or negedge rst_n) 
if(!rst_n) begin
    step_i<=0; step_i_return<=0; 
    tx_en<=0; tx_data<=0;
    cnt_delay<=0; cnt_led1<=0; oLED1<=0;
end
else begin
    case(step_i)
        0: //delay 1s. gap between each ADC convertion.
            // if(cnt_delay>=32'd5_000) begin cnt_delay<=0; step_i<=step_i+1; end
            // else begin cnt_delay<=cnt_delay+1; end
            if(tick_100KHz) begin step_i<=step_i+1; end
        1: //Jump to transmit frame head bytes.
            begin tx_data_16bits<=frame_head; step_i<=90; step_i_return<=step_i+1; end
        2:  //capture ADC.
            if(adc_valid) begin adc_en<=0; tx_data_16bits<=adc_data; step_i<=step_i+1; end
            else begin adc_en<=1; end
        3: //Jump to transmit 16bits ADC value.
            begin step_i<=90; step_i_return<=step_i+1; end
        4:
            begin 
                step_i<=0; 
                ///////////////////////////////////////////////////////////////
                if(cnt_led1>=8'h03-1) begin cnt_led1<=0; oLED1<=~oLED1; end
                else begin cnt_led1<=cnt_led1+1; end
            end
        //////////////////////////////////////////////////////////////////
        90: //Tx High 8 bits.
            begin 
                if(tx_done) begin tx_en<=0; step_i<=step_i+1; end
                else begin tx_en<=1; tx_data<=tx_data_16bits[15:8]; end
            end
        91: //Tx Low 8 bits.
            begin
                if(tx_done) begin tx_en<=0; step_i<=step_i+1; end
                else begin tx_en<=1; tx_data<=tx_data_16bits[7:0]; end
            end
        92:
            begin step_i<=step_i_return; end
            
        default:
                begin step_i<=0; end
    endcase
end
endmodule