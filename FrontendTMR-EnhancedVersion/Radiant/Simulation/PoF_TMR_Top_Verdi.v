//TMR chip sample frequency: 100KHz.
//Leading head byte: 0x55.
//AD7988-5 data width: 16-bits.
//Temperature: 8-bits.
//Total data bandwidth: (8+16+8)*100KHz=3.2Mbps.

//The UART band rate is 4Mbps,
//for a single byte transfer, there are start bit(1)+data bits(8)+stop bit(1)=10-bits.
//for the valid bandwidth is 4Mbps/100kHz=40bits.
//40-bits-(start+stop)*4=32-bits.

//We are using the 100% UART capacity.
//Must use 2x16 FIFO to reduce latency!!!!

module PoF_TMR_Top_Verdi(

    //PCB Onboard oscillator 12MHz.
	input wire iClk_12MHz,
    input wire iRst_N,
	
    //Data Upload and Command Download.
    output wire oData_TxD,
    input wire iCmd_RxD,

    //AD7988 SPI-Compatible Interface.
	output wire oSPI_SDI,
	output wire oSPI_CNV,
	output wire oSPI_SCK,
	input wire iSPI_SDO, 

    //The realistic FIFO Write FPS(Frame per Second). Used to measured by an oscilloscope.
    output wire oFIFOWrFps, //IO-23.

    //FIFO Is Full.
    output wire oFIFO_isFull, //IO-21.

    //UART Tx FPS(Frame per Second) Signal.
    output wire oTxFps, //IO-19.

    //TMP117 I2C Interface.
    output wire oTMP117_SCL,
    inout wire ioTMP117_SDA,
    input wire iTMP117_ALERT,

    //Debug LED*3.
	output wire oLED0, //Sample AD7988 and write data into FIFO.
    output wire oLED1, //UART Tx Indicator.
    output wire oLED2 //Read Temperature Sensor Indicator.
)/* synthesis RGB_TO_GPIO = "oLED0, oLED1" */;


//HSOSC
//High-frequency oscillator.
//Generates 48-MHz nominal clock, +/- 10 percent, with user-programmable divider. 
//Can drive global clock network or fabric routing.
//Input Ports
//CLKHFPU :Power up the oscillator. After power up, output will be stable after 100 ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢
//CLKHFEN :Enable the clock output. 
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
// HSOSC #(.CLKHF_DIV("0b00")) //48 MHz
// my_HSOSC(
//     .CLKHFPU(1'b1), 
//     .CLKHFEN(1'b1), 
//     .CLKHF(clk_48MHz)
// )/* synthesis ROUTE_THROUGH_FABRIC= 0 */; //the value can be either 0 or 1

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
wire rst_short_n;
wire clk_48MHz_Global;
wire clk_48MHz_Fabric;
// ZPLL ic_pll(
// 	//.ref_clk_i(clk_48MHz), 
// 	.ref_clk_i(iClk_12MHz), //External on board oscillator.
// 	.rst_n_i(1'b1), 
// 	.lock_o(rst_short_n), 
// 	.outcore_o(clk_48MHz_Fabric), 
// 	.outglobal_o(clk_48MHz_Global)
// );
assign clk_48MHz_Global=iClk_12MHz;

//long reset.
reg rst_n;
// reg [31:0] cntRst;
// always @(posedge clk_48MHz_Global or negedge rst_short_n) 
// if(!rst_short_n) begin rst_n<=0; cntRst<=0; end
// else begin 
//     cntRst<=(cntRst==32'hFFFFF0)?(cntRst):(cntRst+1);
//     rst_n<=(cntRst==32'hFFFFF0)?(1):(0);
// end
assign rst_n=iRst_N;

//sample data at 100KHz from AD7988.
/////////////////////////////////////////////////
//AD7988-1. Maximum Throughput Rate:100KHz.
localparam TICK_MAX=480; //48MHz/100KHz=480. //single pulse.
//localparam TICK_MAX=240; //48MHz/100KHz/2=240.  //50% Duty Cycle.

reg [15:0] cnt_100KHz;
always @(posedge clk_48MHz_Global or negedge rst_n) 
if(!rst_n) begin cnt_100KHz<=0; end
else begin
    cnt_100KHz<=(cnt_100KHz==TICK_MAX-1)?(0):(cnt_100KHz+1);
end
wire tickCapture;
assign tickCapture=(cnt_100KHz==TICK_MAX-1)?(1):(0);
/////////////////////////////////////////////////////////////////////////
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
reg [2:0] fsmADC;
reg [15:0] captured_data;
always @(posedge clk_48MHz_Global or negedge rst_n) 
if(!rst_n) begin fsmADC<=0; end
else begin
    case(fsmADC)
    0: 
        begin fsmADC<=(tickCapture)?(fsmADC+1):(fsmADC); end
    1: 
        if(adc_valid) begin adc_en<=0; captured_data<=adc_data; fsmADC<=fsmADC+1; end
        else begin adc_en<=1; end
    2:
        begin fsmADC<=0; end
    endcase
end

//////////////////////////////////////////////////////////////////////////
reg [7:0] Temperature_Latest;
reg [15:0] temp2;

//////////////////////////////////////////////////////////////////////
//Since the temperature changes slowly, acquires it every 1 second.
//TMP117 Temperature Sensor.
reg TMP117_En;
wire TMP117_DataValid;
reg [1:0] TMP117_Cmd;
reg [7:0] TMP117_RegAddr;
reg [15:0] TMP117_RegData;
wire [15:0] TMP117_RdData;
reg [7:0] Temp_Result={8'h00};
reg [7:0] Config_Reg={8'h01}; //default value = 0x0220.
reg [7:0] THigh_Limit={8'h02};
reg [7:0] TLow_Limit={8'h03};
reg [7:0] Temp_Offset={8'h07};
reg [7:0] Device_ID={8'h0F};  //default value = 0x0117.
ZTMP117_Controller  myTMP117(
	.iClk(clk_48MHz_Global), //input clock.
	.iRstN(rst_n),
	.iEn(TMP117_En),

    //00: Read Device_ID regiter.
    //01: Read Temp_Result register.
    .iCommand(TMP117_Cmd), 
    .iRegAddr(TMP117_RegAddr),
    .iRegData(TMP117_RegData),

    //I2C Interface.
    .oSCL(oTMP117_SCL),
    .ioSDA(ioTMP117_SDA),

	//acquisition data output interface.
	.oRdData(TMP117_RdData),
	.oDataValid(TMP117_DataValid)
);
//driven by step_i.
reg [7:0] step_i; 
reg [31:0] cnt_delay;
reg Temp_LED;
//UART Protocol Format:
//55 High-Byte Low-Byte Temperature-Byte.
//0.0078125=2^(-7)
//if MSB is 1, negative.
//expand 1 bit for symbol.
reg unsigned [15:0] SensorData; 
reg symbol_bit;
reg [3:0] reg_iterator;
always @(posedge clk_48MHz_Global or negedge rst_n) 
if(!rst_n) begin 
    step_i<=0; cnt_delay<=0; TMP117_En<=0; 
    Temperature_Latest<=0; Temp_LED<=0; reg_iterator<=0; 
end
else begin
    case(step_i)
    0: //Temperature changes slow, read data every 1 second.
        //48MHz/1Hz=48_000_000
        if(cnt_delay==/*1000*/32'd48_000_000-1) begin cnt_delay<=0; step_i<=step_i+1; end
        else begin cnt_delay<=cnt_delay+1; end
    1: //Read Temperature.
        if(TMP117_DataValid) begin 
            TMP117_En<=0; 
            //only update when reads Temp_Result register.
            //SensorData<=(reg_iterator==5)?(TMP117_RdData):(SensorData);
            SensorData<=TMP117_RdData;
            temp2<=TMP117_RdData; 
            reg_iterator<=(reg_iterator==5)?(0):(reg_iterator+1);
            step_i<=step_i+1; 
        end
        else begin 
            TMP117_En<=1; TMP117_Cmd<=2'b00; 
            // case(reg_iterator)
            // 0: begin TMP117_RegAddr<=Device_ID; end
            // 1: begin TMP117_RegAddr<=Config_Reg; end
            // 2: begin TMP117_RegAddr<=THigh_Limit; end
            // 3: begin TMP117_RegAddr<=TLow_Limit; end
            // 4: begin TMP117_RegAddr<=Temp_Offset; end
            // 5: begin TMP117_RegAddr<=Temp_Result; end
            // default: begin TMP117_RegAddr<=Temp_Result; end
            // endcase
            TMP117_RegAddr<=Temp_Result; 
        end
    2: //1.convert BuMa to Yuan.
        begin
            SensorData<=(SensorData[15])?(~SensorData+1):(SensorData);
            symbol_bit<=(SensorData[15])?(1):(0);
            Temp_LED<=~Temp_LED; 
            step_i<=step_i+1; 
        end
    3:  //2.do multiplication to get the real temperature.
        //multiply 0.0078125 , 2^(-7)=1/128=0.0078125, right shift 7 bits.
        begin SensorData<=SensorData>>7; step_i<=step_i+1; end
    4: //3.convert YuanMa to BuMa.
        begin 
            SensorData<=(symbol_bit)?(~SensorData+1):(SensorData); step_i<=step_i+1; 
        end
    5:
        begin Temperature_Latest<={symbol_bit,SensorData[6:0]}; step_i<=0; end
    default:
        begin step_i<=0; end
    endcase
end
assign oLED2=Temp_LED; 

//Tx Random UART Data at 1Mbps.
//generate 1MHz Clock, //48MHz/1MHz=48.
//generate 4MHz Clock, //48MHz/4MHz=12.
//generate 8MHz Clock, //48MHz/8MHz=6.
reg [15:0] tx_data_16bits;
reg [7:0] tx_data;
wire tx_done;

ZUART_Tx #(.Freq_divider(12)) uart_u1
(
	.iClk(clk_48MHz_Global),
	.iRst_N(rst_n),
	.iData(tx_data),
	
	//pull down iEn to start transmition until pulse done oDone was issued.
	.iEn(1), //always enable Tx Module.
	.oDone(tx_done),
	.oTxD(oData_TxD)
);

reg [7:0] fsmUART;
always @(posedge clk_48MHz_Global or negedge rst_n) 
if(!rst_n) begin fsmUART<=0; tx_data<=8'h55; end
else begin
    case(fsmUART)
    0: //update tx buffer immediately after tx_done.
        if(tx_done) begin tx_data<=8'h19; fsmUART<=fsmUART+1; end //fifo_data_out[15:8]

    1: //transmit high 8 bits. //update tx buffer immediately after tx_done.
        if(tx_done) begin tx_data<=8'h87; fsmUART<=fsmUART+1; end //fifo_data_out[7:0]
    2: //Tx Temperature byte. -127 degree celsius ~ +128 degree celsius.
        if(tx_done) begin tx_data<=8'h09; fsmUART<=fsmUART+1; end //temperature.
    3: //sync head bytes.
        if(tx_done) begin tx_data<=8'h55; fsmUART<=0; end //temperature.
    default:
            begin fsmUART<=0; tx_data<=8'h55; end
    endcase
end
endmodule