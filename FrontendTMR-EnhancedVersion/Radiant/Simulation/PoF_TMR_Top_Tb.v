`timescale 1ns/1ps

module PoF_TMR_Top_Tb;

initial begin 
    $fsdbDumpfile("My.fsdb");
    $fsdbDumpvars(0,"PoF_TMR_Top_Tb","+all");
end

reg clk;
//initialize clock to 0 to avoid unknown state at time 0.
initial begin clk=0; end
//10.4167ns*2=20.8333ns, f=48MHz.
always #10.4167 clk=~clk;

reg rst_n;
initial begin
    rst_n=1;
    #30 rst_n=0;
    #30 rst_n=1;
end



wire data_txd_o;
reg cmd_rxd_i;

wire spi_sdi_o;
wire spi_cnv_o;
wire spi_sck_o;
reg spi_sdo_i;

wire fifo_wr_fps_o;
wire fifo_is_full_o;
wire tx_fps_o;

wire tmp117_scl_o;
wire tmp117_sda_io;
reg tmp117_alert_i;

wire [2:0] led_o;

 PoF_TMR_Top_Verdi My_PoF_TMR_Verdi(
    //PCB Onboard oscillator 12MHz.
	.iClk_12MHz(clk),
    .iRst_N(rst_n),
	
    //Data Upload and Command Download.
    .oData_TxD(data_txd_o),
    .iCmd_RxD(cmd_rxd_i),

    //AD7988 SPI-Compatible Interface.
	.oSPI_SDI(spi_sdi_o),
	.oSPI_CNV(spi_cnv_o),
	.oSPI_SCK(spi_sck_o),
	.iSPI_SDO(spi_sdo_i), 

    //The realistic FIFO Write FPS(Frame per Second). Used to measured by an oscilloscope.
    .oFIFOWrFps(fifo_wr_fps_o), //IO-23.

    //FIFO Is Full.
    .oFIFO_isFull(fifo_is_full_o), //IO-21.

    //UART Tx FPS(Frame per Second) Signal.
    .oTxFps(tx_fps_o), //IO-19.

    //TMP117 I2C Interface.
    .oTMP117_SCL(tmp117_scl_o),
    .ioTMP117_SDA(tmp117_sda_io),
    .iTMP117_ALERT(tmp117_alert_i),

    //Debug LED*3.
	.oLED0(led_o[0]), //Sample AD7988 and write data into FIFO.
    .oLED1(led_o[1]), //UART Tx Indicator.
    .oLED2(led_o[2]) //Read Temperature Sensor Indicator.
);


endmodule