/*
* filename: zad7988_controller.v
* function: ad7988 works in 3 wires mode, acquire data and output.
* date: April 4,2024.
* author: Shell Albert 
* 
* SCK Period (CS Mode), VIO above 1.71V, tSCK=22nS(Min), f=45MHz.
* therefore we set SCK to 48MHz/2=24MHz.
*/
module ZAD7988_Controller(
	//common signals.
	input wire iClk, //input clock.
	input wire iRstN,
	input wire iEn,

	//AD7988 SPI-Compatible Interface.
	output wire oSDI,
	output reg oCNV,
	output reg oSCK,
	input wire iSDO, 

	//acquisition data output interface.
	output reg [15:0] oData,
	output reg oDataValid
);

//With SDI tied to VIO(1.8V),
//a rising edge on CNV initiates a conversion, select the CS mode.
//and forces SDO to high impedance.
assign oSDI=1;

reg [15:0] SDO_Shift_In;

//generate 8MHz clock.
//48MHz/6=8MHz. 
//48MHz/4=12MHz. t=84ns.
//48MHz/2=24MHz. t=42ns.
reg [15:0] cntClkPrescale;
always @(posedge iClk or negedge iRstN)
if(!iRstN) begin cntClkPrescale<=0; end
else begin
	if(iEn) begin
		if(cntClkPrescale==2-1) begin cntClkPrescale<=0; end
		else begin cntClkPrescale<=cntClkPrescale+1; end
	end
	else begin cntClkPrescale<=0; end
end
wire tick_Clk;
assign tick_Clk=(cntClkPrescale==2-1)?1:0;

//Driven by step i.
reg [7:0] step_i;
reg [7:0] cnt_delay;
reg [15:0] cnt_test;
always @(posedge iClk or negedge iRstN)
if(!iRstN) begin
	step_i<=0; cnt_delay<=0; 
	oCNV<=0; oSCK<=0; oData<=0; oDataValid<=0; SDO_Shift_In<=0; cnt_test<=0; 
end
else begin
	if(iEn) begin
			case(step_i)
			0:
				begin oCNV<=1; step_i<=step_i+1; end
			1:
				//give ADC adequate time to convert.(10 clocks period)
				//AD7988-1,Conversion time,CNV Rising Edge to Data Available, 9.5uS(Max).
				//AD7988-5,Conversion time,CNV Rising Edge to Data Available, 1.6uS(Max).
				//9.5us/42ns=226.2
				//1.6uS/42nS=38.1
				if(cnt_delay==60-1) begin cnt_delay<=0; oCNV<=0; step_i<=step_i+1; end
				else begin cnt_delay<=cnt_delay+1; end
			2: //output 16 clock.
				if(cnt_delay==16) begin cnt_delay<=0; step_i<=step_i+2; end
				else begin oSCK<=1; cnt_delay<=cnt_delay+1; step_i<=step_i+1; end
			3: //latch data in at falling edge.
				begin oSCK<=0; SDO_Shift_In<={SDO_Shift_In[14:0],iSDO}; step_i<=step_i-1; end
			4:
				begin oDataValid<=1; oData<=cnt_test/*SDO_Shift_In*/; step_i<=step_i+1; end
			5:
				begin oDataValid<=0; cnt_test<=cnt_test+1; step_i<=0; end
			default:
				begin step_i<=0; end
			endcase
	end
	else begin 
		step_i<=0; oDataValid<=0; oData<=0; SDO_Shift_In<=0; oCNV<=0; oSCK<=0;
	end
end

endmodule


