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
    input wire [15:0] iTempData,

    //UART Tx FPS (Frame Per Second)
    output reg oTxFps
);

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
//UART Protocol Format:
//55 High-Byte Low-Byte Temperature-Byte.
//0.0078125=2^(-7)

//expand 1 bit for symbol.
reg unsigned [15:0] SensorData; 
reg symbol_bit;
reg fetched;
reg fetched2;
reg processed;

always @(posedge iClk or negedge iRstN) 
if(!iRstN) begin
    cntFSM<=0; tx_data<=8'h55; cnt_led<=0; oLED<=0; oFIFORdEn<=0; tx_done_reg<=0; 
    fetched<=0; fetched2<=0; processed<=0; 
end
else begin
    case(cntFSM)
    0: //transmit frame head byte 0x55.
        begin  //update tx buffer immediately after tx_done.
            if(tx_done) begin tx_data<=fifo_data_out[15:8]; cntFSM<=cntFSM+1; end
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
            tx_data<=fifo_data_out[7:0]; 
            fetched<=0; fetched2<=0; //reset fetched flags.
            //pre-processing temperature data.
            SensorData<=SensorData>>7; 
            cntFSM<=cntFSM+1; 
        end
        else begin
            if(!processed) begin 
                //SensorData<=16'h8000; //0xFF,MSB is 1, 0x7F=255. => -255.
                //SensorData<=16'hF380; //0x98,MSB is 1, 0x18=24. => -24.
                //SensorData<=16'h3200; //0x64,MSB is 0, 0x64=100. => +100.
                //pre-processing temperature data.
                SensorData<=(iTempData[15])?(16'hFFFF-iTempData):(iTempData);
                symbol_bit<=(iTempData[15])?(1):(0); 
                processed<=1; 
            end
        end
    2: //transmit low 8 bits.
        if(tx_done) begin 
            //Tx Temperature byte. -128 degree celsius ~ +127 degree celsius.
            tx_data<={symbol_bit,SensorData[6:0]};
            processed<=0; //reset. 
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
            end
        end
    default:
            begin cntFSM<=0; fetched<=0; fetched2<=0; processed<=0; end
    endcase
end
endmodule