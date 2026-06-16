module ZUART_Adapter(
    //common signals.
	input wire iClk, //input clock.
	input wire iRstN,
	input wire iEn,

    //UART.
    output wire oUART_TxD,
    output reg oLED,

    //Read Data from FIFO.
    input wire iFIFOEmptyFlag,
    output reg oFIFORdEn,
    input wire [15:0] iFIFODataOut,

    //Temperature Updated.
    input wire [15:0] iTempData
);

//Tx Random UART Data at 1Mbps.
//generate 1MHz Clock, //48MHz/1MHz=48.
//generate 4MHz Clock, //48MHz/4MHz=12.
//generate 8MHz Clock, //48MHz/8MHz=6.
reg [15:0] tx_data_16bits;
reg [7:0] tx_data;
reg tx_en;
wire tx_done;
ZUART_Tx #(.Freq_divider(6)) uart_u1
(
	.iClk(iClk),
	.iRst_N(iRstN),
	.iData(tx_data),
	
	//pull down iEn to start transmition until pulse done oDone was issued.
	.iEn(tx_en),
	.oDone(tx_done),
	.oTxD(oUART_TxD)
);

//Driven by step_i.
reg [7:0] step_i;
reg [15:0] fifo_data_out;
reg [31:0] cnt_led;
//0x00: Temperature (High 8 bits)
//0x01: Temperature (Low 8 bits)
//0x02: Bus Current (High 8 bits)
//0x03: Bus Current (Low 8 bits)
//0x04: Bus Voltage (High 8 bits)
//0x05: Bus Voltage (Low 8 bits)
//0x06: Power Consumption (High 8 bits)
//0x07: Power Consumption (Low 8 bits)
reg [7:0] auxiliary_iterate; 
reg [7:0] check_sum;
always @(posedge iClk or negedge iRstN) 
if(!iRstN) begin
    step_i<=0; tx_en<=0; tx_data<=0; auxiliary_iterate<=0; cnt_led<=0; oLED<=0; 
end
else begin
    case(step_i)
        0: //transmit frame head byte 0x55.
            if(tx_done) begin tx_en<=0; check_sum<=8'h55; step_i<=step_i+1; end
            else begin tx_en<=1; tx_data<=8'h55; end
        1: //Fetch data from FIFO when it's not empty.
            if(!iFIFOEmptyFlag) begin oFIFORdEn<=1; step_i<=step_i+1; end
        2: //fetch data at the 2nd clock after ReadEnable.
            begin fifo_data_out<=/*16'h1987*/iFIFODataOut; oFIFORdEn<=0; step_i<=step_i+1; end
        3: //transmit high 8 bits.
            if(tx_done) begin tx_en<=0; check_sum<=check_sum+fifo_data_out[15:8]; step_i<=step_i+1; end
            else begin tx_en<=1; tx_data<=fifo_data_out[15:8]; end
        4: //transmit low 8 bits.
            if(tx_done) begin tx_en<=0; check_sum<=check_sum+fifo_data_out[7:0]; step_i<=step_i+1; end
            else begin tx_en<=1; tx_data<=fifo_data_out[7:0]; end
        5: //Transmit Auxiliary Data Leading Byte.
            if(tx_done) begin tx_en<=0; check_sum<=check_sum+auxiliary_iterate; step_i<=step_i+1; end
            else begin tx_en<=1; tx_data<=auxiliary_iterate; end
        6: //Prepare Auxiliary Data.
            begin
                case(auxiliary_iterate)
                8'h00: //0x00: Temperature (High 8 bits)
                    begin tx_data<=iTempData[15:8]; end
                8'h01: //0x01: Temperature (Low 8 bits)
                    begin tx_data<=iTempData[7:0]; end
                8'h02: //0x02: Bus Current (High 8 bits)
                    begin tx_data<=8'h22; end
                8'h03: //0x03: Bus Current (Low 8 bits)
                    begin tx_data<=8'h33; end
                8'h04: //0x04: Bus Voltage (High 8 bits)
                    begin tx_data<=8'h44; end
                8'h05: //0x05: Bus Voltage (Low 8 bits)
                    begin tx_data<=8'h55; end
                8'h06: //0x06: Power Consumption (High 8 bits)
                    begin tx_data<=8'h66; end
                8'h07: //0x07: Power Consumption (Low 8 bits)
                    begin tx_data<=8'h77; end
                default: 
                    begin tx_data<=8'h11; end
                endcase
                /////////////////////////////////////////////
                auxiliary_iterate<=(auxiliary_iterate==8'h07)?(0):(auxiliary_iterate+1); 
                step_i<=step_i+1;
            end
        7: //Transmit Auxiliary Data Byte.
            if(tx_done) begin tx_en<=0; check_sum<=check_sum+tx_data; step_i<=step_i+1; end
            else begin tx_en<=1; end
        8: //Transmit Checksum. 
            begin
                if(tx_done) begin tx_en<=0; step_i<=0; end
                else begin tx_en<=1; tx_data<=check_sum; end
                /////////////////////////////////////////////////////////////////
                cnt_led<=(cnt_led==32'hFFFFF-1)?(0):(cnt_led+1);
                oLED<=(cnt_led==32'hFFFFF-1)?(~oLED):(oLED);
            end
        default:
                begin step_i<=0; end
    endcase
end
endmodule