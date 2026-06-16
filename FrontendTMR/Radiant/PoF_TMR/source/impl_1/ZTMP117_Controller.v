/*
* filename: ZTMP117_Controller.v
* function: ad7988 works in 3 wires mode, acquire data and output.
* date: April 4,2024.
* author: Shell Albert 
* 
* SCK Period (CS Mode), VIO above 1.71V, tSCK=22nS(Min), f=45MHz.
* therefore we set SCK to 48MHz/2=24MHz.
*/
module ZTMP117_Controller(
	//common signals.
	input iClk, //input clock.
	input iRstN,
	input iEn,

    //00: Read Device_ID regiter.
    //01: Read Temp_Result register.
    input wire [1:0] iCommand, 
    input wire [7:0] iRegAddr,
    input wire [15:0] iRegData,

    //I2C Interface.
    output reg oSCL,
    inout wire ioSDA,

	//acquisition data output interface.
	output reg [15:0] oRdData,
	output reg oDataValid
);

//bi-directional SDA.
reg ioSDA_Dir; //1: Output, 0:Input.
reg oSDA;
assign ioSDA=(ioSDA_Dir)?(oSDA):(1'bz);
wire iSDA;
assign iSDA=ioSDA; 

//ADD0 is connected to GND, address is 1001000x.
reg [7:0] SlaveAddress={7'b1001000,1'b0};
reg [7:0] SlaveAddress_Rd={7'b1001000,1'b1};
reg [7:0] Temp_Result={8'h00};
reg [7:0] Config_Reg={8'h01};
reg [7:0] Device_ID={8'h0F};

//TMP117 SCL works in 1Khz~400KHz.
//48MHz/250kHz/2=96.
reg [7:0] cnt_250KHz;
always @(posedge iClk or negedge iRstN)
if(!iRstN) begin cnt_250KHz<=0; end
else begin
    cnt_250KHz<=(iEn)?((cnt_250KHz==96-1)?(0):(cnt_250KHz+1)):(0);
end
wire tick_250KHz;
assign tick_250KHz=(cnt_250KHz==96-1)?(1):(0);

//driven by step_i.
reg [7:0] step_i;
reg [7:0] cnt_bits;
reg [7:0] Read_Register_Addr;
always @(posedge iClk or negedge iRstN)
if(!iRstN) begin 
    step_i<=0; 
    oSCL<=1; ioSDA_Dir<=1; oSDA<=1; 
    cnt_bits<=0; 
    oDataValid<=0; 
end
else begin
    case({iEn,iCommand})
    {1'b1,2'b00}: //Read Register.
        begin
            case(step_i)
            0: //Start Signal. //SDA Outputs.
                if(tick_250KHz) begin oSCL<=1; ioSDA_Dir<=1; oSDA<=1; step_i<=step_i+1; end
            1: //SDA from High to Low while SCL is High.
                if(tick_250KHz) begin oSDA<=0; step_i<=step_i+1; end
//////////////////////////////////////////////////////////////////////////////
            2: //Slave Address.
                if(tick_250KHz) begin oSCL<=0; oSDA<=SlaveAddress[7-cnt_bits]; step_i<=step_i+1; end
            3: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            4:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            5: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            6:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
/////////////////////////////////////////////////////////////////////////////////////////////
            7: //Register Pointer(N). SDA Outputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=iRegAddr[7-cnt_bits]; step_i<=step_i+1; end 
            8: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            9:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            10: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            11:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
//////////////////////////////////////////////////////////////////////////////////////////////
            12: //RESTART. Pull SDA from 1 to 0 while SCL is 1. SDA Outputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=1; step_i<=step_i+1; end 
            13:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            14:
                if(tick_250KHz) begin oSDA<=0; step_i<=step_i+1; end
///////////////////////////////////////////////////////////////////////////////////////////
            15: //Register Pointer(N).
                if(tick_250KHz) begin oSCL<=0; oSDA<=SlaveAddress_Rd[7-cnt_bits]; step_i<=step_i+1; end 
            16: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            17:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            18: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            19:
               if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end    
////////////////////////////////////////////////////////////////////////////////////////
            20: //High 8-bits. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end 
            21: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            22: 
                if(tick_250KHz) begin 
                    oRdData<={oRdData[14:0],iSDA}; 
                    if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                    else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
                end
            23: //ACK from Master. //SDA Outputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=0; step_i<=step_i+1; end
            24:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end 
///////////////////////////////////////////////////////////////////////////////////
            25: //Low 8-bits. SDA Inputs.
                if(tick_250KHz)begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end 
            26: //TMP117 latches data in at rising edge.
                if(tick_250KHz)begin oSCL<=1; step_i<=step_i+1; end
            27: 
                if(tick_250KHz) begin
                    oRdData<={oRdData[14:0],iSDA}; 
                    if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                    else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
                end
            28: //NACK from Master. SDA Outputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=1; step_i<=step_i+1; end
            29:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
////////////////////////////////////////////////////////////////////////////////////////
            30: //Stop Signal, Pull SDA from 0 to 1 while SCL is 1. 
                if(tick_250KHz) begin oSDA<=0; step_i<=step_i+1; end
            31: 
                if(tick_250KHz) begin oSDA<=1; step_i<=step_i+1; end
/////////////////////////////////////////////////////////////////////////////////////////
            32:
                begin oDataValid<=1; step_i<=step_i+1; end
            33:
                begin oDataValid<=0; step_i<=0; end   
            endcase
        end
    {1'b1,2'b01}: //Write Register.
        begin 
            case(step_i)
            0: //Start Signal. //SDA Outputs.
                if(tick_250KHz) begin oSCL<=1; ioSDA_Dir<=1; oSDA<=1; step_i<=step_i+1; end
            1: //SDA from High to Low while SCL is High.
                if(tick_250KHz) begin oSDA<=0; step_i<=step_i+1; end
//////////////////////////////////////////////////////////////////////////////
            2: //Slave Address.
                if(tick_250KHz) begin oSCL<=0; oSDA<=SlaveAddress[7-cnt_bits]; step_i<=step_i+1; end
            3: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            4:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            5: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            6:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
/////////////////////////////////////////////////////////////////////////////////
            7: //Register Pointer(N). SDA Outputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=iRegAddr[7-cnt_bits]; step_i<=step_i+1; end 
            8: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            9:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            10: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            11:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
/////////////////////////////////////////////////////////////////////////////////
            12: //Data to register N MSB.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=iRegData[15-cnt_bits]; step_i<=step_i+1; end
            13: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            14:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            15: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            16:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
///////////////////////////////////////////////////////////////////////////////////
            17: //Data to register N MSB.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=1; oSDA<=iRegData[7-cnt_bits]; step_i<=step_i+1; end
            18: //TMP117 latches data in at rising edge.
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
            19:
                if(cnt_bits==8-1) begin cnt_bits<=0; step_i<=step_i+1; end
                else begin cnt_bits<=cnt_bits+1; step_i<=step_i-2; end
            20: //ACK from slave. SDA Inputs.
                if(tick_250KHz) begin oSCL<=0; ioSDA_Dir<=0; step_i<=step_i+1; end
            21:
                if(tick_250KHz) begin oSCL<=1; step_i<=step_i+1; end
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
            22: //Stop Signal, Pull SDA from 0 to 1 while SCL is 1. 
                if(tick_250KHz) begin oSDA<=0; step_i<=step_i+1; end
            23: 
                if(tick_250KHz) begin oSDA<=1; step_i<=step_i+1; end
/////////////////////////////////////////////////////////////////////////////////////////
            24:
                begin oDataValid<=1; step_i<=step_i+1; end
            25:
                begin oDataValid<=0; step_i<=0; end   
            endcase
        end
    default:
        begin oSCL<=1; ioSDA_Dir<=1; oSDA<=1; step_i<=0; oDataValid<=0; end
    endcase
end
/////////////////////////////////////////////////////////////////////////////////////////
endmodule