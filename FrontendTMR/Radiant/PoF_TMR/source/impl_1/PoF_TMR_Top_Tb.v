`timescale 1ns/1ps

module PoF_TMR_Top_Tb;


reg clk;
initial begin clk=0; end
always #1 clk=~clk;

reg rst_n;
initial begin
    rst_n=1;
    #100 rst_n=0;
    #100 rst_n=1;
end


initial begin 
    $fsdbDumpfile("My.fsdb");
    $fsdbDumpvars(0,"PoF_TMR_Top_Tb","+all");
end


endmodule