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
    input wire [7:0] iTempData,
    input wire [15:0] iTempData2,

    //UART Tx FPS (Frame Per Second)
    output reg oTxFps
);
reg [15:0] Temp_Shadow;

reg tx_done_reg;
always @(posedge iClk or negedge iRstN)
if(!iRstN) begin oTxFps<=0; end
else begin
    oTxFps<=(tx_done_reg)?(~oTxFps):(oTxFps);
end

//Tx Random UART Data at 1Mbps.
//generate 1MHz Clock, //48MHz/1MHz=48.
//generate 4MHz Clock, //48MHz/4MHz=12.
//generate 8MHz Clock, //48MHz/8MHz=6.
reg [15:0] tx_data_16bits;
reg [7:0] tx_data;
wire tx_done;

ZUART_Tx #(.Freq_divider(12)) uart_u1
(
	.iClk(iClk),
	.iRst_N(iRstN),
	.iData(tx_data),
	
	//pull down iEn to start transmition until pulse done oDone was issued.
	.iEn(1), //always enable Tx Module.
	.oDone(tx_done),
	.oTxD(oUART_TxD)
);


//Driven by step_i.
reg [7:0] cntFSM;
reg [31:0] cnt_led;
reg [15:0] fifo_data_out;

reg fetched;
reg fetched2;

always @(posedge iClk or negedge iRstN) 
if(!iRstN) begin
    cntFSM<=0; tx_data<=8'h55; cnt_led<=0; oLED<=0; oFIFORdEn<=0; tx_done_reg<=0; 
    fetched<=0; fetched2<=0;
    Temp_Shadow<=0; 
end
else begin
    case(cntFSM)
    0: //transmit frame head byte 0x55.
        begin  //update tx buffer immediately after tx_done.
            if(tx_done) begin tx_data<=fifo_data_out[15:8]/*Temp_Shadow[15:8]*/; cntFSM<=cntFSM+1; end
            //////////////////////////////////////////////////////
            //fetch data from FIFO when it's not empty.
            if(!iFIFOEmptyFlag && !fetched && !fetched2) begin oFIFORdEn<=1; fetched<=1; end
            else begin 
                if(fetched) begin //clock latency is 2 for reading.
                    fifo_data_out<=/*16'h1987*/iFIFODataOut; oFIFORdEn<=0; 
                    fetched<=0; fetched2<=1; 
                end
                else begin 
                    oFIFORdEn<=0; 
                end
            end
            ////////////////////////////////////////////
            cnt_led<=(cnt_led==1000_000-1)?(0):(cnt_led+1);
            oLED<=(cnt_led==1000_000-1)?(~oLED):(oLED);
        end
    1: //transmit high 8 bits. //update tx buffer immediately after tx_done.
        if(tx_done) begin 
            tx_data<=fifo_data_out[7:0]/*Temp_Shadow[7:0]*/; 
            fetched<=0; fetched2<=0; //reset fetched flags.
            cntFSM<=cntFSM+1; 
        end
    2: //transmit low 8 bits.
        if(tx_done) begin 
            //Tx Temperature byte. -127 degree celsius ~ +128 degree celsius.
            tx_data<=iTempData/*Temp_Shadow[7:0]*/;
            tx_done_reg<=1; 
            cntFSM<=cntFSM+1; 
        end
    3:
        begin
            tx_done_reg<=0;
            ////////////////////////////////////
            if(tx_done) begin 
                tx_data<=8'h55; 
                cntFSM<=0; 
                Temp_Shadow[7:0]<=iTempData; //cache temperature using low 8-bits.
            end
        end
    default:
            begin cntFSM<=0; fetched<=0; fetched2<=0; end
    endcase
end
endmodule