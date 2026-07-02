module ZAD7988_Adapter(
    //common signals.
	input wire iClk, //input clock.
	input wire iRstN,
	input wire iEn,

	//AD7988 SPI-Compatible Interface.
	output wire oSDI,
	output wire oCNV,
	output wire oSCK,
	input wire iSDO, 

    //The realistic FIFO write rate. Used to measured by an oscilloscope.
    output reg oFIFOWrFps,

    //Write Data Into FIFO.
    input wire iFIFOFullFlag,
    output reg oFIFOWrEn,
    output reg[15:0] oFIFODataIn,

    //Working LED indicator.
    output reg oLED
);

/////////////////////////////////////////////////
reg adc_en;
wire adc_valid;
wire [15:0] adc_data;
ZAD7988_Controller u1_ad7988(
	//common signals.
	.iClk(iClk), //input clock.
	.iRstN(iRstN),
	.iEn(adc_en),

	//AD7988 SPI-Compatible Interface.
	.oSDI(oSDI),
	.oCNV(oCNV),
	.oSCK(oSCK),
	.iSDO(iSDO), 

	//acquisition data output interface.
	.oData(adc_data),
	.oDataValid(adc_valid)
);

//AD7988-1. Maximum Throughput Rate:100KHz.
//48MHz/100KHz/2=240.
localparam TICk_MAX=240;
//48MHz/50KHz/2=480.
//localparam TICk_MAX=480;


reg [15:0] cnt_100KHz;
reg wrFIFO_Fps;

always @(posedge iClk or negedge iRstN) 
if(!iRstN) begin
    cnt_100KHz<=0; oFIFOWrFps<=0;
end
else begin
    cnt_100KHz<=(iEn)?((cnt_100KHz==TICk_MAX-1)?(0):(cnt_100KHz+1)):(0);
    oFIFOWrFps<=(iEn)?((wrFIFO_Fps)?(~oFIFOWrFps):(oFIFOWrFps)):(0);
end
wire tickFPS;
assign tickFPS=(cnt_100KHz==TICk_MAX-1)?(1):(0);

/////////////////////////////////////////////////
//Driven by step_i.
reg [7:0] step_i;
reg [15:0] cnt_test;
reg [15:0] cnt_led; //LED flashes at 100KHz/2.
always @(posedge iClk or negedge iRstN)
if(!iRstN) begin 
    step_i<=0; adc_en<=0; oFIFOWrEn<=0; oFIFODataIn<=0; cnt_test<=0; cnt_led<=0; oLED<=0; wrFIFO_Fps<=0;
end
else begin
    if(iEn) begin
        case(step_i)
        0: //100KHz Trigger.
        begin
            if(tickFPS) begin cnt_test<=cnt_test+1; step_i<=step_i+1; end
            ///////////////////////////////////////////////////////////////////////
            oFIFOWrEn<=0; 
            wrFIFO_Fps<=0; 
            cnt_led<=(cnt_led==50_000-1)?(0):(cnt_led+1);
            oLED<=(cnt_led==50_000-1)?(~oLED):(oLED);
        end
        1: //Read ADC Data.
            if(adc_valid) begin adc_en<=0; oFIFODataIn<=cnt_test/*adc_data*/; step_i<=step_i+1; end
            //if(adc_valid) begin adc_en<=0; oFIFODataIn<=adc_data; step_i<=step_i+1; end
            else begin adc_en<=1; end
            //begin oFIFODataIn<=cnt_test; step_i<=step_i+1; end
        2: //Write Data into FIFO if FIFO is not full.
            if(!iFIFOFullFlag) begin oFIFOWrEn<=1; wrFIFO_Fps<=1; step_i<=0; end
        default:
            begin step_i<=0; end
        endcase
    end
    else begin
        step_i<=0; adc_en<=0; oFIFOWrEn<=0; oFIFODataIn<=0; wrFIFO_Fps<=0; 
    end
end
/////////////////////////////////////////////////////////////////////////
endmodule

